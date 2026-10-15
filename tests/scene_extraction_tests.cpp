#include "scene/SceneExtraction.h"
#include "editor/CollisionWire.h"

#include <array>
#include <cmath>
#include <iostream>
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
void scopedPlayerLightingExtraction() {
    Scene scene;
    auto parent=entity(1,"Affine player parent",0,math::translation({10,2,3})*math::scaling({2,3,4}));
    auto player=entity(2,"Player",1,math::translation({1,0,2}));player.character.emplace();
    auto hero=entity(3,"Hero mesh",2,Mat4{});hero.model=AssetRef<ModelAsset>{"assets/models/hero/manifest.json"};
    auto floor=entity(4,"Floor",0,Mat4{});floor.model=hero.model;
    auto unlit=entity(5,"Independent scopes",2,Mat4{});unlit.model=hero.model;
    unlit.lightingScopes={0,lighting_scope::Character,lighting_scope::World};
    auto lamp=entity(6,"Authored lamp",0,Mat4{});lamp.point.emplace();
    lamp.point->roles.receiverMask=lighting_scope::World;lamp.point->roles.shadowCasterMask=lighting_scope::Character;
    lamp.point->nearFalloffRadius=2.75f;lamp.point->specularStrength=.35f;
    lamp.point->rangeFalloffExponent=2.5f;lamp.point->edgeSoftness=.6f;lamp.point->sourceRadius=.8f;lamp.point->attenuation=-1.5f;
    auto sun=entity(7,"Authored sun",0,Mat4{});sun.directional.emplace();
    auto& settings=player.character->lightRig;settings.enabled=true;
    settings.fill.enabled=true;settings.key.direction={-.3f,-.8f,.4f};settings.localWorld.height=4;
    settings.localWorld.radius=13;settings.localWorld.rangeFalloffExponent=.75f;settings.localWorld.edgeSoftness=.4f;
    settings.localWorld.sourceRadius=.7f;settings.localWorld.attenuation=3.5f;
    player.character->darknessReveal={true,11};
    scene.apply({floor,hero,player,parent,lamp,sun,unlit},{0,0,player.id});
    const auto before=scene.records();const auto extracted=extractScene(scene);
    require(extracted.models.size()==3&&extracted.points.size()==2&&extracted.directional.size()==1,
        "Separated rig did not append exactly one world source.");
    const auto& character=extracted.models[0];const auto& world=extracted.models[1];const auto& independent=extracted.models[2];
    require(character.id==hero.id&&character.lightingScopes==SurfaceLightingScopes{2,2,2},"Player descendants retained World lighting membership.");
    require(world.id==floor.id&&world.lightingScopes==SurfaceLightingScopes{1,1,1},"World receiver gained Character lighting membership.");
    require(independent.lightingScopes==SurfaceLightingScopes{0,2,0},"Independent direct/caster/ambient authoring scopes were conflated.");
    require(character.lightingCategory==lighting_scope::Character&&independent.lightingCategory==lighting_scope::Character&&
        world.lightingCategory==lighting_scope::World,"Transient surface category was inferred from opt-out receiver scopes.");
    require(extracted.points.front().roles==lamp.point->roles&&extracted.directional.front().roles==sun.directional->roles,
        "Extraction dropped authored world light roles.");
    require(extracted.points.front().nearFalloffRadius==2.75f&&extracted.points.front().specularStrength==.35f&&
        extracted.points.front().rangeFalloffExponent==2.5f&&extracted.points.front().edgeSoftness==.6f&&
        extracted.points.front().sourceRadius==.8f&&extracted.points[1].rangeFalloffExponent==.75f&&
        extracted.points[1].edgeSoftness==.4f&&extracted.points[1].sourceRadius==.7f&&
        extracted.points.front().attenuation==-1.5f&&extracted.points[1].attenuation==3.5f,
        "Extraction dropped independent ordinary/player point response controls.");
    const auto playerPosition=math::transformPoint(parent.local*player.local,{});
    require(close(extracted.points[1].position,playerPosition+Vec3{0,settings.localWorld.height,0})&&
        extracted.points[1].radius==13&&
        extracted.points[1].roles.receiverMask==lighting_scope::World&&
        extracted.points[1].roles.shadowCasterMask==lighting_scope::World&&
        !extracted.points[1].roles.ambientReveal,"Local World Light lost finite range or inherited scale/Character membership.");
    require(extracted.characterLights[0].enabled&&extracted.characterLights[1].enabled&&
        close(extracted.characterLights[0].direction,math::normalized(settings.key.direction))&&
        extracted.characterShadow.enabled&&close(extracted.characterShadow.position,playerPosition)&&
        close(extracted.characterShadow.direction,math::normalized(settings.key.direction)),
        "Extraction lost separate character illumination or shadow.");
    const auto frozen=extracted.points;
    require(extracted.playerReveal.enabled&&extracted.playerReveal.spread==11&&
        close(extracted.playerReveal.position,playerPosition+Vec3{0,player.character->height*.5f,0}),
        "Player darkness range inherited parent scale or lost the presented center.");
    const std::array poses{ScenePoseUpdate{player.id,math::translation({1,0,2})*math::rotation(math::axisAngle({0,1,0},1.7))}};
    scene.applyRuntimePoses(poses);const auto turned=extractScene(scene);
    require(close(turned.points[1].position,frozen[1].position)&&turned.characterLights==extracted.characterLights&&
        turned.characterShadow==extracted.characterShadow,
        "Turning the player rotated a world-aligned rig offset.");
    require(turned.playerReveal==extracted.playerReveal,"Turning the player moved the darkness range.");
    const std::array moved{ScenePoseUpdate{player.id,math::translation({2,0,2})}};
    scene.applyRuntimePoses(moved);const auto presented=extractScene(scene);
    require(close(presented.points[1].position,frozen[1].position+Vec3{2,0,0})&&
        presented.points[1].entityId==frozen[1].entityId&&presented.characterLights==extracted.characterLights&&
        presented.points[1].radius==frozen[1].radius&&
        presented.points[1].rangeFalloffExponent==frozen[1].rangeFalloffExponent&&
        close(presented.characterShadow.position,extracted.characterShadow.position+Vec3{2,0,0}),
        "Local rig did not follow presented translation with stable source identities.");
    require(close(presented.playerReveal.position,extracted.playerReveal.position+Vec3{2,0,0}),
        "Player darkness range did not follow presented world translation.");
    require(scene.records().size()==before.size(),"Transient actor rig contributions became authored entities.");
    settings.enabled=false;scene.apply({floor,hero,player,parent,lamp,sun,unlit},{0,0,player.id});
    require(extractScene(scene).points.size()==1&&extractScene(scene).playerReveal.enabled&&
        !extractScene(scene).characterLights[0].enabled&&!extractScene(scene).characterShadow.enabled,
        "Disabled local rig retained contributions or disabled the separate darkness range.");
    player.character->darknessReveal.enabled=false;scene.apply({floor,hero,player,parent,lamp,sun,unlit},{0,0,player.id});
    require(!extractScene(scene).playerReveal.enabled,"Disabled darkness range retained its player contribution.");
    settings.enabled=true;scene.apply({floor,hero,player,parent,lamp,sun,unlit},{0,0,player.id});
    const auto revealOff=extractScene(scene);
    require(!revealOff.playerReveal.enabled&&revealOff.points.size()==2&&revealOff.points[1].radius==13,
        "Disabling reveal removed or resized the independent Local World Light.");
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
void collisionPicking() {
    Scene scene;
    auto parent=entity(1,"Parent",0,math::translation({3,0,-5})*math::scaling({2,3,.5f}));
    auto box=entity(2,"Box collider",1,Mat4{});box.collider=SceneCollider{{2,2,2}};
    auto distant=entity(3,"Distant collider",0,math::translation({3,0,-9}));distant.collider=SceneCollider{{2,2,2}};
    scene.apply({parent,box,distant},{});
    require(pickCollisionShape(scene,{{3,0,0},{0,0,-1}},100)==2,"Clicking a box interior did not pick the closest affine collider.");
    require(!pickCollisionShape(scene,{{3,0,0},{0,0,-1}},4),"Collision picking exceeded the camera far distance.");
    require(!pickCollisionShape(scene,{{7,0,0},{0,0,-1}},100),"A ray outside the box selected its collider.");
    require(pickCollisionShape(scene,{{3,0,-5},{1,0,0}},100)==2,"A camera inside a collider could not select it.");
    box.local=math::scaling({0,1,1});scene.apply({parent,box,distant},{});
    require(pickCollisionShape(scene,{{3,0,0},{0,0,-1}},100)==3,"A collapsed collider blocked the valid hit behind it.");

    auto character=entity(4,"Capsule",0,math::translation({0,0,-4}));character.character=SceneCharacter{};
    character.character->height=2;character.character->radius=.5f;scene.apply({character},{});
    require(pickCollisionShape(scene,{{0,1,0},{0,0,-1}},100)==4,"Capsule cylinder interior was not selectable.");
    require(pickCollisionShape(scene,{{0,1.9f,0},{0,0,-1}},100)==4,"Capsule hemisphere was not selectable.");
    require(!pickCollisionShape(scene,{{.49f,1.99f,0},{0,0,-1}},100),"Capsule picking incorrectly used a box at the rounded corner.");
    require(!pickCollisionShape(scene,{{0,1,0},{0,0,1}},100),"Collision shapes behind the camera remained selectable.");
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
    require(box.triangles.size()==12 && box.edges.size()==18,"Box collision wire omitted its face diagonals.");
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
    try { run(); lightOrientation(); proceduralSunExtraction();scopedPlayerLightingExtraction();gizmoAffineRotation();particleExtraction();collisionPicking();clippedSelectionWires();collisionFaces();std::cout<<"Scene extraction checks passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
