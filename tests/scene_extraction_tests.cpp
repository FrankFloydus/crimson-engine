#include "scene/SceneComponents.h"
#include "scene/SceneExtraction.h"
#include "editor/CollisionWire.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <set>
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

void actorComponents(EntityRecord& record) {
    record.actor.emplace();record.controller.emplace();record.movement.emplace();record.navigation.emplace();record.playerInput.emplace();
}

void run() {
    Scene scene;
    const auto rotation = math::rotation(math::axisAngle({0,1,0},std::numbers::pi/2));
    auto parent=entity(1,"Parent",0,math::translation({10,2,3})*rotation);
    auto model=entity(2,"Nested model",1,math::translation({2,0,0})*math::scaling({-2,3,4}));
    model.model=AssetRef<ModelAsset>{"assets/models/first/manifest.json"};
    model.meshShading=MeshShading::Smooth;
    auto other=entity(3,"Repeated model",0,math::translation({-5,0,0}));
    other.model=model.model;
    other.meshShading=MeshShading::Flat;
    auto missing=entity(4,"Missing model",0,math::translation({7,0,0}));
    missing.model=AssetRef<ModelAsset>{"assets/models/missing/manifest.json"};
    Mat4 shear;
    shear[4]=.8f; shear[8]=.2f; shear[9]=.7f;
    auto sun=entity(5,"Sun",1,shear*math::scaling({2,3,4}));
    sun.directional=SceneDirectionalLight{{.2f,.4f,.8f},7,false};
    sun.directional->shadowAngularDiameter=1.25f;
    auto point=entity(6,"Point",1,math::translation({0,4,1})*math::scaling({10,20,30}));
    point.point=ScenePointLight{{.8f,.3f,.1f},23,true};
    scene.apply({point,sun,missing,other,model,parent},{});
    const auto extracted=extractScene(scene);
    require(extracted.models.size()==3,"Extraction dropped a model or unresolved model reference.");
    require(extracted.models[0].id==2 && extracted.models[1].id==3 && extracted.models[2].id==4,
            "Extraction ordering must be deterministic across registry allocation order.");
    require(extracted.models[0].assetId==extracted.models[1].assetId,"Repeated models lost their shared asset identity.");
    require(extracted.models[0].meshShading==MeshShading::Smooth&&extracted.models[1].meshShading==MeshShading::Flat&&
        extracted.models[2].meshShading==MeshShading::UseMeshNormals,"Extraction lost independent mesh shading on shared or missing assets.");
    require(close(extracted.models[0].world,parent.local*model.local),"Nested model did not accumulate its parent transform.");
    require(extracted.models[2].assetId==missing.model->id,"An unresolved asset reference was discarded.");
    require(extracted.directional.size()==1 && extracted.points.size()==1,"Light components were not extracted.");
    require(close(extracted.directional[0].direction,math::normalized({-1,-.7f,.2f})),
            "Directional light discarded its authored forward axis under affine parenting.");
    require(!extracted.directional[0].enabled && extracted.directional[0].intensity==7 && extracted.directional[0].color==sun.directional->color,
            "Directional light properties were altered during extraction.");
    require(close(extracted.points[0].position,math::transformPoint(parent.local*point.local,{0,0,0})),"Point light ignored its parent transform.");
    require(extracted.directional[0].shadowAngularDiameter==1.25f,"Extraction lost directional angular diameter.");
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
void orientedSelectionBoxes() {
    Scene scene;
    Mat4 shear;shear[4]=.35f;shear[8]=-.2f;
    auto parent=entity(1,"Group",0,math::translation({4,2,-3})*math::eulerRotationXYZ({.2f,.7f,.1f})*shear*math::scaling({-2,3,.5f}));
    auto model=entity(2,"Rotated long mesh",1,math::translation({5,1,2})*math::eulerRotationXYZ({.1f,.6f,.3f}));
    model.model=AssetRef<ModelAsset>{"assets/models/selection/manifest.json"};
    auto collider=entity(3,"Auxiliary collision",2,math::translation({20,0,0}));collider.collider=SceneCollider{{100,100,100}};
    auto other=entity(4,"Separate mesh",0,math::translation({-5,0,7})*math::eulerRotationXYZ({0,.4f,0}));other.model=model.model;
    auto marker=entity(5,"Empty marker",0,math::translation({30,2,-8}));
    auto character=entity(6,"Character",0,math::translation({1,0,3}));character.controller=SceneCharacterController{};
    auto particle=entity(7,"Authored particle marker",0,math::translation({-20,4,2}));particle.particle=AssetRef<ParticleEffectAsset>{};
    scene.apply({parent,model,collider,other,marker,character,particle},{});
    const Aabb local{{-2,-.25f,-.5f},{2,.75f,.5f},true};unsigned modelQueries{},collisionQueries{};
    const ModelBoundsQuery query=[&](const std::string&)->std::optional<Aabb>{++modelQueries;return local;};
    const CollisionMeshQuery collision=[&](const std::string&)->CollisionMeshResult{++collisionQueries;return {};};
    const auto boxes=selectionBoxes(scene,std::array<EntityId,1>{2},query,collision);
    require(boxes.size()==1&&boxes[0].id==2&&close(boxes[0].local.minimum,local.minimum)&&close(boxes[0].local.maximum,local.maximum)&&
        close(boxes[0].world,parent.local*model.local),"Selection outline inflated a rotated mesh or discarded reflected/sheared parenting.");
    require(modelQueries==1&&collisionQueries==0,"A visual selection queried auxiliary collision geometry or visited its mesh twice.");
    modelQueries=0;
    const auto grouped=selectionBoxes(scene,std::array<EntityId,3>{1,2,4},query,collision);
    const auto groupLocal=math::transformBounds(local,model.local);
    require(grouped.size()==2&&grouped[0].id==1&&grouped[1].id==4&&modelQueries==2&&collisionQueries==0,
        "Multiple selected roots merged into one world box or duplicated a selected descendant.");
    require(close(grouped[0].local.minimum,groupLocal.minimum)&&close(grouped[0].local.maximum,groupLocal.maximum)&&close(grouped[0].world,parent.local),
        "Group outline included an oversized collider/empty parent marker or transformed descendants twice.");
    require(close(grouped[1].local.minimum,local.minimum)&&close(grouped[1].world,other.local),"An independent selected model lost its own oriented box.");
    const auto direct=selectionBoxes(scene,std::array<EntityId,1>{3},query,collision);
    require(direct.size()==1&&close(direct[0].local.minimum,{-50,-50,-50})&&close(direct[0].local.maximum,{50,50,50})&&
        close(direct[0].world,parent.local*model.local*collider.local),"Direct collider selection did not use its authored local dimensions.");
    const auto nonvisual=selectionBoxes(scene,std::array<EntityId,1>{5},query);
    require(nonvisual.size()==1&&close(nonvisual[0].local.minimum,{-.5f,-.5f,-.5f})&&close(nonvisual[0].local.maximum,{.5f,.5f,.5f})&&
        close(nonvisual[0].world,marker.local),"An empty selection lost its stable marker fallback.");
    const auto actor=selectionBoxes(scene,std::array<EntityId,1>{6},query);
    const auto traversal=resolveAgentTraversal(*character.controller).value();
    require(actor.size()==1&&close(actor[0].local.minimum,{-traversal.radius,0,-traversal.radius})&&
        close(actor[0].local.maximum,{traversal.radius,traversal.height,traversal.radius}),"Character outline invented bounds beyond its authored capsule.");
    const auto authored=selectionBoxes(scene,std::array<EntityId,1>{7},query);
    const auto focused=selectionBounds(scene,std::array<EntityId,1>{7},query,[](EntityId)->std::optional<Aabb>{return Aabb{{-100,-100,-100},{100,100,100},true};});
    const auto stable=selectionBoxes(scene,std::array<EntityId,1>{7},query);
    require(authored.size()==1&&stable.size()==1&&close(authored[0].local.minimum,stable[0].local.minimum)&&close(authored[0].local.maximum,stable[0].local.maximum)&&
        focused.minimum[0]<-90,"Live particle focus changed the stable authored selection outline.");
    // Saved chest pose that exposed the world-AABB inflation (2.86x volume).
    // Detached constants keep this regression independent of authored files.
    auto chest=entity(8,"Chest",0,Mat4{.77279097f,.104876295f,-2.12970018f,0,
        -.68832409f,2.15625024f,-.14358427f,0,2.01812553f,.69527495f,.76654291f,0,.76837599f,-.39015046f,2.68131518f,1});
    chest.model=model.model;Scene chestScene;chestScene.apply({chest},{});
    const Aabb chestLocal{{-.38452151f,0,-.49975586f},{.38452151f,.63623047f,.49975586f},true};
    const auto chestBoxes=selectionBoxes(chestScene,std::array<EntityId,1>{8},[&](const std::string&)->std::optional<Aabb>{return chestLocal;});
    require(chestBoxes.size()==1&&close(chestBoxes[0].local.minimum,chestLocal.minimum)&&close(chestBoxes[0].local.maximum,chestLocal.maximum)&&close(chestBoxes[0].world,chest.local),
        "Chest selection repeated the local/world AABB roundtrip.");
    const auto edge=math::transformDirection(chestBoxes[0].world,{chestLocal.maximum[0]-chestLocal.minimum[0],0,0});
    require(std::abs(math::length(edge)-1.74419023)<1e-4,"Chest selection edge no longer follows its transformed mesh axes.");
}
void authoredPrimitiveBounds() {
    Scene scene;Mat4 shear;shear[4]=.25f;
    auto parent=entity(1,"Parent",0,math::translation({4,2,-3})*shear*math::scaling({-2,3,.5f}));
    auto box=entity(2,"Box",1,math::translation({2,0,1})*math::eulerRotationXYZ({.1f,.4f,.2f}));
    box.model=AssetRef<ModelAsset>{BuiltinBoxModelId};box.primitive.emplace();box.primitive->size={2,3,4};
    auto sphere=entity(3,"Sphere",0,math::translation({-4,1,2}));
    sphere.model=AssetRef<ModelAsset>{BuiltinSphereModelId};sphere.primitive.emplace();sphere.primitive->radius=2;
    auto cylinder=entity(4,"Cylinder",0,math::translation({8,0,3}));
    cylinder.model=AssetRef<ModelAsset>{BuiltinCylinderModelId};cylinder.primitive.emplace();cylinder.primitive->radius=1.5f;cylinder.primitive->height=5;
    scene.apply({parent,box,sphere,cylinder},{});
    const std::array<Aabb,3> localBounds{{{{-1,-1.5f,-2},{1,1.5f,2},true},{{-2,-2,-2},{2,2,2},true},{{-1.5f,-2.5f,-1.5f},{1.5f,2.5f,1.5f},true}}};
    unsigned queries{};
    const ModelBoundsQuery query=[&](const std::string&)->std::optional<Aabb>{++queries;return Aabb{{-100,-100,-100},{100,100,100},true};};
    for(std::size_t index=0;index<localBounds.size();++index) {
        const auto id=static_cast<EntityId>(index+2);const auto world=*scene.worldTransform(id);
        const auto boxes=selectionBoxes(scene,std::array<EntityId,1>{id},query);
        require(boxes.size()==1 && close(boxes[0].local.minimum,localBounds[index].minimum) && close(boxes[0].local.maximum,localBounds[index].maximum) && close(boxes[0].world,world),
            "Primitive selection outline ignored authored geometry dimensions or applied its transform twice.");
        const auto bounds=selectionBounds(scene,std::array<EntityId,1>{id},query),expected=math::transformBounds(localBounds[index],world);
        require(bounds.valid && close(bounds.minimum,expected.minimum) && close(bounds.maximum,expected.maximum),
            "Primitive focus bounds ignored authored dimensions under affine parenting.");
    }
    require(queries==0,"Built-in primitive bounds depended on imported model readiness.");
    const auto extracted=extractScene(scene);
    for(const auto& model:extracted.models)require(model.primitive==scene.find(model.id)->primitive,"Render extraction lost per-entity primitive dimensions.");
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
void proceduralSunExtraction() {
    Scene scene;
    auto parent=entity(1,"Sun parent",0,math::translation({10,20,30})*math::rotation(math::axisAngle({1,0,0},.6)));
    auto sun=entity(2,"Linked sun",1,math::rotation(math::axisAngle({0,1,0},.8))*math::scaling({2,3,4}));
    sun.directional=SceneDirectionalLight{{.8f,.6f,.4f},7,true};
    auto environment=entity(3,"Sky",0,Mat4{});environment.environment.emplace();
    environment.environment->sun=sun.id;environment.environment->environment.source=EnvironmentSource::ProceduralSky;
    auto otherEnvironment=entity(4,"Unlinked sky",0,Mat4{});otherEnvironment.environment.emplace();
    auto otherSun=entity(5,"Other light",0,Mat4{});otherSun.directional.emplace();
    scene.apply({otherSun,environment,parent,otherEnvironment,sun},{environment.id});
    auto extracted=extractScene(scene);
    require(extracted.environment.sun==sun.id && extracted.proceduralSun.enabled &&
        close(extracted.proceduralSun.direction,directionalLightDirection(parent.local*sun.local)*-1.f),
        "Procedural sun did not use the explicitly linked light's world direction toward the sun.");
    require(extracted.proceduralSun.color==sun.directional->color && extracted.proceduralSun.intensity==7,
        "Sun extraction changed authored color/energy or applied scale to intensity.");
    const auto initial=extracted.proceduralSun;
    sun.local=math::translation({-100,300,400})*sun.local;
    scene.apply({otherSun,environment,parent,otherEnvironment,sun},{environment.id});
    require(extractScene(scene).proceduralSun==initial,"Sun translation moved an infinitely distant procedural sun.");
    const std::array updates{ScenePoseUpdate{parent.id,math::rotation(math::axisAngle({1,0,0},1.2))}};
    scene.applyRuntimePoses(updates);extracted=extractScene(scene);
    require(!close(extracted.proceduralSun.direction,initial.direction) &&
        close(extracted.proceduralSun.direction,directionalLightDirection(*scene.worldTransform(sun.id))*-1.f),
        "Runtime sun rotation did not update the extracted frame value.");
    sun.directional->enabled=false;
    scene.apply({otherSun,environment,parent,otherEnvironment,sun},{environment.id});extracted=extractScene(scene);
    require(!extracted.proceduralSun.enabled && extracted.environment.sun==sun.id && extracted.proceduralSun.intensity==7,
        "Disabling a linked sun dropped its authored link or retained visible sunlight.");
    scene.apply({otherSun,environment,parent,otherEnvironment,sun},{otherEnvironment.id});
    require(!extractScene(scene).proceduralSun.enabled,"An unlinked active Environment implicitly selected a directional light.");
    scene.apply({otherSun,environment,parent,otherEnvironment,sun},{});
    require(!extractScene(scene).proceduralSun.enabled,"No active Environment inherited another Environment's sun.");
}
void independentActorOwnership() {
    auto a=entity(10,"Actor A",0,math::translation({1,0,0}));actorComponents(a);
    auto b=entity(20,"Actor B",10,math::translation({2,0,0}));actorComponents(b);
    auto visualA=entity(11,"A visual",10,Mat4{});visualA.model=AssetRef<ModelAsset>{"assets/models/a/manifest.json"};
    auto visualB=entity(21,"B visual",20,Mat4{});visualB.model=visualA.model;
    auto boundary=entity(30,"Unlit actor boundary",10,Mat4{});boundary.actor.emplace();
    auto visualC=entity(31,"Boundary visual",30,Mat4{});visualC.model=visualA.model;
    Scene scene;scene.apply({visualB,visualC,a,b,visualA,boundary},{0,0,a.id});
    const auto first=extractScene(scene);
    require(first.models.size()==3&&first.points.empty(),"Actors added unsolicited light sources.");
    require(first.models[0].id==11&&first.models[0].actorOwner==10&&first.models[1].actorOwner==20&&first.models[2].actorOwner==30,
        "Nested actor boundary leaked its parent rig ownership.");
    auto properties=scene.properties();properties.playerCharacter=b.id;scene.apply(scene.records(),properties);
    const auto switched=extractScene(scene);
    for(size_t i=0;i<first.models.size();++i)require(switched.models[i].actorOwner==first.models[i].actorOwner,"Input ownership changed nested actor membership.");
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
void cameraPoseConversion() {
    Mat4 parentShear;parentShear[4]=.6f;parentShear[8]=-.3f;parentShear[9]=.2f;
    auto parent=entity(1,"Camera parent",0,math::translation({4,2,-3})*math::eulerRotationXYZ({.3f,.7f,-.2f})*parentShear*math::scaling({-2,.7f,3}));
    Mat4 localShear;localShear[4]=-.4f;localShear[8]=.25f;localShear[9]=.5f;
    auto authored=entity(53,"Inactive rolled Camera",1,math::translation({1,3,-2})*math::eulerRotationXYZ({.4f,-.5f,2.7f})*localShear*math::scaling({.8f,1.7f,1.2f}));
    authored.camera=SceneCamera{73,.037f,345};authored.visible=false;
    auto active=entity(7,"Active Camera",0,math::translation({8,0,4}));active.camera.emplace();
    Scene scene;scene.apply({authored,parent,active},{NoEntity,active.id});
    const auto resolved=resolveSceneCamera(scene,authored.id),game=resolveGameCamera(scene);
    require(resolved&&game&&close(game->viewMatrix(),resolveSceneCamera(scene,active.id)->viewMatrix()),
        "Resolving an inactive scene camera changed active Game camera semantics.");
    require(!resolveSceneCamera(scene,parent.id)&&!resolveSceneCamera(scene,999),"A non-camera or missing entity resolved a view.");
    const auto oldWorld=*scene.worldTransform(authored.id);const auto oldRevision=scene.revision();
    require(close(resolved->position,math::transformPoint(oldWorld,{}))&&resolved->fov==73&&resolved->nearPlane==.037f&&resolved->farPlane==345&&
        resolved->fovAxis==CameraFovAxis::Vertical,"Arbitrary camera resolution lost affine position or vertical lens settings.");
    const auto unchanged=sceneCameraLocalTransform(scene,authored.id,*resolved);
    require(unchanged&&*unchanged==authored.local,"A no-op camera pose conversion introduced transform drift.");
    auto navigated=*resolved;navigated.position={12,3,-8};navigated.target=navigated.position+math::normalized(Vec3{.3f,-.4f,-.8f});
    navigated.up=math::rotate(Vec3{0,1,0},navigated.forward(),2.6);
    const auto local=sceneCameraLocalTransform(scene,authored.id,navigated);
    require(local&&math::isAffine(*local),"Navigated camera pose could not be converted beneath a reflected/sheared parent.");
    auto moved=authored;moved.local=*local;Scene candidate;candidate.apply({moved,parent,active},{NoEntity,active.id});
    const auto roundTrip=resolveSceneCamera(candidate,authored.id);const auto newWorld=*candidate.worldTransform(authored.id);
    require(roundTrip&&close(roundTrip->viewMatrix(),navigated.viewMatrix())&&close(roundTrip->position,navigated.position),
        "Piloted camera re-resolution changed the desired world view or roll.");
    require(roundTrip->fov==resolved->fov&&roundTrip->nearPlane==resolved->nearPlane&&roundTrip->farPlane==resolved->farPlane&&
        roundTrip->fovAxis==CameraFovAxis::Vertical,"Camera pose conversion altered authored vertical FOV or clipping planes.");
    const auto oldResidual=resolved->viewMatrix()*oldWorld,newResidual=roundTrip->viewMatrix()*newWorld;
    for(unsigned column=0;column<3;++column)for(unsigned row=0;row<3;++row)
        require(std::abs(oldResidual.at(row,column)-newResidual.at(row,column))<1e-4f,
            "Camera piloting discarded view-relative stretch, shear or reflection.");
    require(math::handedness(oldWorld)==math::handedness(newWorld)&&scene.revision()==oldRevision&&scene.find(authored.id)->local==authored.local,
        "Pose conversion mutated the source scene or changed affine handedness.");
    const auto restored=sceneCameraLocalTransform(candidate,authored.id,*resolved);
    require(restored&&close(*restored,authored.local),"Returning to the original view did not preserve the complete authored affine transform.");
    for(unsigned invalid=0;invalid<5;++invalid) {
        auto bad=navigated;
        if(invalid==0)bad.target=bad.position;
        if(invalid==1)bad.up={};
        if(invalid==2)bad.up=bad.target-bad.position;
        if(invalid==3)bad.position[0]=std::numeric_limits<float>::quiet_NaN();
        if(invalid==4)bad.up[1]=std::numeric_limits<float>::infinity();
        require(!sceneCameraLocalTransform(scene,authored.id,bad),"Degenerate/nonfinite camera pose produced an authored transform.");
    }
    require(!sceneCameraLocalTransform(scene,parent.id,navigated)&&!sceneCameraLocalTransform(scene,999,navigated),
        "Non-camera pose conversion produced an authored transform.");
    auto singular=authored;singular.parent=NoEntity;singular.local=math::scaling({0,1,1});
    Scene collapsed;collapsed.apply({singular},{});
    require(resolveSceneCamera(collapsed,singular.id)&&!sceneCameraLocalTransform(collapsed,singular.id,navigated),
        "A singular camera frame was accepted for piloting or changed existing view resolution semantics.");
    auto collapsedParent=parent;collapsedParent.local=math::scaling({0,1,1});singular=authored;singular.local={};
    collapsed.apply({collapsedParent,singular},{});
    require(!sceneCameraLocalTransform(collapsed,singular.id,navigated),"A singular parent produced a piloted local transform.");
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
    // Parent selection wires/pivots use authored bounds. Live particles may
    // move far away, expire or restart without changing that selection box.
    const auto authored=selectionBounds(scene,nestedSelection,{});
    const Aabb expanded{{-80,-20,-50},{130,80,100},true};
    const auto framed=selectionBounds(scene,nestedSelection,{},[&](EntityId)->std::optional<Aabb>{return expanded;});
    const auto stable=selectionBounds(scene,nestedSelection,{});
    require(close(authored.minimum,stable.minimum) && close(authored.maximum,stable.maximum) &&
        framed.maximum[0]>stable.maximum[0],"Live particle framing changed the stable authored selection bounds.");
    auto copied=extracted;particle.local=math::translation({100,0,0});scene.apply({particle,parent},{});
    require(close(copied.particles[0].world,extracted.particles[0].world),"An extracted particle frame retained mutable scene storage.");
}
void hierarchicalVisibility() {
    auto group=entity(1,"Hidden group",0,Mat4{});group.visible=false;
    auto model=entity(2,"Hidden model",1,Mat4{});model.model=AssetRef<ModelAsset>{"assets/models/hidden/manifest.json"};
    auto particle=entity(3,"Hidden emitter",1,Mat4{});particle.particle=AssetRef<ParticleEffectAsset>{"assets/particles/hidden.crimson-particle"};
    auto sun=entity(4,"Hidden sun",1,Mat4{});sun.directional.emplace();
    auto lamp=entity(5,"Hidden lamp",1,Mat4{});lamp.point.emplace();
    auto player=entity(6,"Hidden player",1,Mat4{});actorComponents(player);
    auto visibleGroup=entity(9,"Visible group",0,Mat4{});
    auto visible=entity(7,"Visible model",9,math::translation({2,0,0}));visible.model=AssetRef<ModelAsset>{"assets/models/visible/manifest.json"};
    auto hidden=entity(8,"Authored hidden",0,Mat4{});hidden.model=model.model;hidden.visible=false;
    auto farHidden=entity(10,"Hidden group child",9,math::translation({100,0,0}));farHidden.model=model.model;farHidden.visible=false;
    auto camera=entity(11,"Hidden camera marker",1,math::translation({0,0,3}));camera.camera.emplace();
    auto environment=entity(12,"Hidden Environment marker",1,Mat4{});environment.environment.emplace();environment.environment->exposure=2;environment.environment->sun=sun.id;
    auto collider=entity(13,"Hidden collision",1,math::translation({0,0,-3}));collider.collider.emplace();
    collider.collider->shape=ColliderShape::Trimesh;collider.collider->source=*model.model;
    auto rearCollider=entity(14,"Visible collision",0,math::translation({0,0,-8}));rearCollider.collider.emplace();
    Scene scene;const SceneProperties properties{environment.id,camera.id,player.id};
    auto records=std::vector{group,model,particle,sun,lamp,player,visibleGroup,visible,hidden,farHidden,camera,environment,collider,rearCollider};
    scene.apply(records,properties);const auto extracted=extractScene(scene);
    require(extracted.models.size()==4&&extracted.models[0].id==2&&!extracted.models[0].visible&&
        extracted.models[1].id==7&&extracted.models[1].visible&&!extracted.models[2].visible&&!extracted.models[3].visible,
        "Hierarchical visibility dropped animation identities or failed to mark hidden models.");
    require(extracted.particles.size()==1&&extracted.particles.front().id==particle.id&&!extracted.particles.front().visible,
        "Hidden emitter was dropped from simulation reconciliation or retained visible presentation.");
    require(extracted.directional.empty()&&extracted.points.empty()&&!extracted.proceduralSun.enabled,
        "Hidden hierarchy leaked authored or player-rig lighting contributions.");
    require(resolveGameCamera(scene).has_value()&&extracted.environment.exposure==2&&extracted.environment.sun==sun.id,
        "Hiding editor markers disabled the active Camera or changed global Environment settings.");
    std::size_t modelQueries{},collisionQueries{},particleQueries{};
    const ModelBoundsQuery bounds=[&](const std::string& id)->std::optional<Aabb>{
        ++modelQueries;require(id==visible.model->id,"Selection requested hidden model geometry.");return Aabb{{-1,-1,-1},{1,1,1},true};
    };
    const CollisionMeshQuery collision=[&](const std::string&)->CollisionMeshResult{++collisionQueries;return {};};
    require(!selectionBounds(scene,std::array<EntityId,1>{group.id},bounds,[&](EntityId)->std::optional<Aabb>{++particleQueries;return Aabb{};},collision).valid,
        "Hidden selected hierarchy retained focus or pivot bounds.");
    require(selectionBoxes(scene,std::array<EntityId,1>{group.id},bounds,collision).empty()&&!modelQueries&&!collisionQueries&&!particleQueries,
        "Hidden selected hierarchy retained outline geometry or requested hidden resources.");
    const auto boxes=selectionBoxes(scene,std::array<EntityId,2>{group.id,visibleGroup.id},bounds,collision);
    require(boxes.size()==1&&boxes.front().id==visibleGroup.id&&close(boxes.front().local.minimum,{1,-1,-1})&&close(boxes.front().local.maximum,{3,1,1}),
        "A hidden descendant inflated its visible parent's outline.");
    require(pickCollisionShape(scene,{{0,0,0},{0,0,-1}},100,collision)==rearCollider.id,
        "Hidden collider or Character intercepted editor picking.");
    require(!collisionQueries,"Editor picking requested hidden original collision geometry.");
    records.front().visible=true;scene.apply(records,properties);const auto restored=extractScene(scene);
    require(restored.models[0].visible&&restored.particles.front().visible&&restored.directional.size()==1&&restored.points.size()==1&&
        restored.proceduralSun.enabled,
        "Showing a hierarchy did not restore its visual contributions.");
    require(!restored.models[2].visible&&!restored.models[3].visible,
        "Showing a parent overwrote a child's authored hidden state.");
}
void collisionPicking() {
    Scene scene;
    auto parent=entity(1,"Parent",0,math::translation({3,0,-5})*math::scaling({2,3,.5f}));
    auto box=entity(2,"Box collider",1,Mat4{});box.collider=SceneCollider{{2,2,2}};
    auto distant=entity(3,"Distant collider",0,math::translation({3,0,-9}));distant.collider=SceneCollider{{2,2,2}};
    auto volume=entity(5,"Wire-only navigation volume",NoEntity,Mat4{});volume.navigationVolume.emplace();volume.navigationVolume->size={100,100,100};
    scene.apply({parent,box,distant,volume},{});
    require(pickCollisionShape(scene,{{3,0,0},{0,0,-1}},100)==2,"Clicking a box interior did not pick the closest affine collider.");
    require(!pickCollisionShape(scene,{{3,0,0},{0,0,-1}},4),"Collision picking exceeded the camera far distance.");
    require(!pickCollisionShape(scene,{{7,0,0},{0,0,-1}},100),"A ray outside the box selected its collider.");
    require(!pickCollisionShape(scene,{{3,0,0},{0,0,-1}},100,{}, {},false),
        "An empty navigation-volume interior intercepted background deselection while collision overlays were hidden.");
    require(pickCollisionShape(scene,{{3,0,-5},{1,0,0}},100)==2,"A camera inside a collider could not select it.");
    box.local=math::scaling({0,1,1});scene.apply({parent,box,distant},{});
    require(pickCollisionShape(scene,{{3,0,0},{0,0,-1}},100)==3,"A collapsed collider blocked the valid hit behind it.");

    auto character=entity(4,"Capsule",0,math::translation({0,0,-4}));character.controller=SceneCharacterController{};
    auto profile=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{newAssetId(),"Picking capsule",{2,.5f,45,.3f}});
    character.controller->profile=AssetRef<NavigationAgentProfile>{profile->id};scene.apply({character},{});
    const NavigationAgentProfileQuery profiles=[&](const auto& id){return id==profile->id?profile:nullptr;};
    require(pickCollisionShape(scene,{{0,1,0},{0,0,-1}},100,{},profiles)==4,"Capsule cylinder interior was not selectable.");
    require(pickCollisionShape(scene,{{0,1.9f,0},{0,0,-1}},100,{},profiles)==4,"Capsule hemisphere was not selectable.");
    require(!pickCollisionShape(scene,{{.49f,1.99f,0},{0,0,-1}},100,{},profiles),"Capsule picking incorrectly used a box at the rounded corner.");
    require(!pickCollisionShape(scene,{{0,1,0},{0,0,1}},100,{},profiles),"Collision shapes behind the camera remained selectable.");
}
void clippedSelectionWires() {
    const Vec4 viewport{20,30,800,600};
    for(const auto depth:{ClipDepth::NegativeOneToOne,ClipDepth::ZeroToOne}) {
        const auto projection=math::perspective(std::numbers::pi/2,4.0/3,1,20,depth);
        const auto crossing=projectClippedSegment({0,0,1},{.5f,0,-5},projection,viewport,depth);
        require(crossing.has_value(),"A selection edge crossing the camera/near plane disappeared entirely.");
        const auto wide=projectClippedSegment({-100,0,-4},{100,0,-4},projection,viewport,depth);
        require(wide && std::abs((*wide)[0][0]-20)<1e-3 && std::abs((*wide)[1][0]-820)<1e-3,
                "Selection edges were not clipped to both viewport sides before projection.");
        require(!projectClippedSegment({0,0,1},{0,1,2},projection,viewport,depth),"A selection edge wholly behind the camera was drawn.");
        require(!projectClippedSegment({0,0,-30},{0,1,-40},projection,viewport,depth),"An edge beyond the far plane was drawn.");
        require(!projectClippedSegment({0,0,-4},{0,1,-4},projection,{0,0,0,600},depth),"A zero-width viewport produced a line.");
    }
}
void collisionFaces() {
    const Vec4 viewport{20,30,800,600};
    for(const auto depth:{ClipDepth::NegativeOneToOne,ClipDepth::ZeroToOne}) {
        const auto projection=math::perspective(std::numbers::pi/2,4.0/3,1,20,depth);
        const auto crossing=projectClippedTriangle({0,2,1},{-2,-1,-5},{2,-1,-5},projection,viewport,depth);
        require(crossing.count>=3,"A collision fill crossing the camera near plane disappeared.");
        for(unsigned i=0;i<crossing.count;++i)require(math::finite(crossing.vertices[i]) &&
            crossing.vertices[i][0]>=19.99f && crossing.vertices[i][0]<=820.01f &&
            crossing.vertices[i][1]>=29.99f && crossing.vertices[i][1]<=630.01f,
            "Clipped collision fill escaped the viewport or produced a nonfinite vertex.");
        require(!projectClippedTriangle({0,0,1},{1,0,1},{0,1,1},projection,viewport,depth).count,
            "Collision fill behind the camera was retained.");
        require(!projectClippedTriangle({0,0,-21},{1,0,-21},{0,1,-21},projection,viewport,depth).count,
            "Collision fill past the far plane was retained.");
        require(!projectClippedTriangle({0,0,-4},{1,0,-4},{0,1,-4},projection,{0,0,0,600},depth).count,
            "An empty viewport retained collision fill.");
    }
    const auto closed=[](const CollisionDebugMesh& mesh,auto point,const Vec3& center) {
        std::map<std::array<std::uint32_t,2>,unsigned> edgeUses;
        for(const auto& triangle:mesh.triangles) {
            const auto a=point(mesh.vertices.at(triangle[0])),b=point(mesh.vertices.at(triangle[1])),c=point(mesh.vertices.at(triangle[2]));
            require(math::dot(math::cross(b-a,c-a),(a+b+c)/3-center)>0,
                "Collision fill has an inward-facing or degenerate triangle.");
            for(unsigned i=0;i<3;++i) {
                const auto x=triangle[i],y=triangle[(i+1)%3];++edgeUses[{std::min(x,y),std::max(x,y)}];
            }
        }
        for(const auto& [edge,uses]:edgeUses)require(uses==2,"Collision fill is not closed along a triangle edge.");
    };
    const auto& box=boxCollisionMesh();
    require(box.triangles.size()==12 && box.edges.size()==12,"Box collision wire must contain twelve edges without face diagonals.");
    closed(box,[](const Vec3& point){return point;},{});
    for(const auto height:{2.f,5.f}) {
        const auto& capsule=capsuleCollisionMesh(height,1);
        closed(capsule,[&](const Vec3& point){return capsuleCollisionPoint(point,height,1);},{0,height*.5f,0});
        for(const auto& vertex:capsule.vertices) {
            const auto point=capsuleCollisionPoint(vertex,height,1);
            const auto center=Vec3{0,std::clamp(point[1],1.f,height-1),0};
            require(std::abs(math::length(point-center)-1)<1e-5,"Collision debug capsule diverged from the physical capsule.");
        }
        require(capsule.edges.size()==(height==2?384u:516u),"Capsule wires must contain Godot's contours, not fill triangle edges.");
        unsigned rings=0,sides=0,caps=0;
        std::set<std::array<long,6>> uniqueSegments;
        for(const auto& edge:capsule.edges) {
            const auto a=capsuleCollisionPoint(capsule.wireVertices.at(edge[0]),height,1);
            const auto b=capsuleCollisionPoint(capsule.wireVertices.at(edge[1]),height,1);
            require(math::length(b-a)>1e-6,"Capsule wire contains a zero-length side.");
            for(const auto point:{a,b}) {
                const auto center=Vec3{0,std::clamp(point[1],1.f,height-1),0};
                require(std::abs(math::length(point-center)-1)<1e-5,"Capsule contour left the collision surface.");
            }
            if(a[1]==b[1] && (a[1]==1 || a[1]==height-1))++rings;
            else if(a[0]==b[0] && a[2]==b[2]) {
                require(a[1]==1 && b[1]==height-1 && (std::abs(a[0])==1 || std::abs(a[2])==1),
                    "Capsule cylinder contains a non-cardinal or subdivided side.");
                ++sides;
            } else {
                require((a[0]==0 && b[0]==0) || (a[2]==0 && b[2]==0),"Capsule cap contains a diagonal triangulation wire.");
                require((a[1]<=1 && b[1]<=1) || (a[1]>=height-1 && b[1]>=height-1),"Capsule arc crossed its cylinder.");
                ++caps;
            }
            std::array<long,3> first{},last{};
            for(unsigned axis=0;axis<3;++axis) {first[axis]=std::lround(a[axis]*100000);last[axis]=std::lround(b[axis]*100000);}
            if(last<first)std::swap(first,last);
            require(uniqueSegments.insert({first[0],first[1],first[2],last[0],last[1],last[2]}).second,
                "Capsule wire overdraws a duplicate contour.");
        }
        require(rings==(height==2?128u:256u) && sides==(height==2?0u:4u) && caps==256,
            "Capsule lost an equator ring, cardinal side or hemisphere contour.");
    }
}
}
int main() {
    try { run(); orientedSelectionBoxes();authoredPrimitiveBounds();lightOrientation(); proceduralSunExtraction();independentActorOwnership();gizmoAffineRotation();cameraPoseConversion();particleExtraction();hierarchicalVisibility();collisionPicking();clippedSelectionWires();collisionFaces();std::cout<<"Scene extraction checks passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
