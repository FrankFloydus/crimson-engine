#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("cr-light-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("cr-light-"))std::filesystem::remove_all(target,error);}
};
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
std::string snapshot(const SceneDocument& document){return serializeSceneSnapshot({document.assetId(),document.displayName(),document.scene().records(),document.scene().properties(),document.previewCamera()});}
LightFlickerSettings authoredFlicker(){
    LightFlickerSettings value;
    value.enabled=true;value.behavior=LightFlickerBehavior::FailingFluorescent;value.playback=LightFlickerPlayback::Once;
    value.speed=1.25f;value.duration=12;value.startDelay=.2f;value.timeOffset=-.125f;value.seed=UINT32_MAX;
    value.timingRandomness=.6f;value.brightnessRandomness=.4f;value.slowAmount=.25f;value.slowRate=.8f;
    value.flutterAmount=.5f;value.flutterRate=12;value.smoothness=.65f;value.brightnessBias=-.1f;
    value.stableMinimum=1;value.stableMaximum=3;value.failureMinimum=.2f;value.failureMaximum=.4f;value.failureLevel=.1f;
    value.flashesMinimum=3;value.flashesMaximum=6;value.flashMinimum=.03f;value.flashMaximum=.12f;
    value.gapMinimum=.04f;value.gapMaximum=.2f;value.restMinimum=2;value.restMaximum=5;value.eventFrequency=2;
    value.riseTime=.04f;value.fallTime=.08f;value.recoveryTime=.2f;value.afterglow=.15f;
    value.period=2;value.pulseWidth=.3f;value.secondaryStrength=.4f;value.secondarySpacing=.25f;
    value.intensity={true,.1f,1.4f,true,1.5f};
    value.color.enabled=true;value.color.minimumTint={.8f,.7f,.6f};value.color.maximumTint={.9f,1,.85f};
    value.color.minimumTemperatureShift=-200;value.color.maximumTemperatureShift=300;value.color.invert=true;value.color.response=.75f;value.color.amount=.6f;
    value.radius={true,.7f,1.2f,false,2};return value;
}
void persistenceHistoryAndIsolation(){
    Project project;SceneDocument document(project.root);const auto point=document.createPointLight(),sun=document.createDirectionalLight();
    require(point&&sun&&document.save("Light controls"),document.status());
    const auto originalPoint=*document.scene().find(point),originalSun=*document.scene().find(sun);
    require(originalPoint.point->colorMode==LightColorMode::Color&&originalSun.directional->colorMode==LightColorMode::Color&&
        !originalPoint.point->flicker.enabled&&!originalSun.directional->flicker.enabled,"Existing light defaults changed.");
    const auto settings=authoredFlicker();const auto commands=document.history().size();
    const auto tune=[&](auto& records,auto&){for(auto& record:records){
        if(record.point){record.point->colorMode=LightColorMode::Temperature;record.point->temperatureKelvin=2800;record.point->flicker=settings;}
        if(record.directional){record.directional->colorMode=LightColorMode::Temperature;record.directional->temperatureKelvin=7500;record.directional->flicker=settings;}
    }};
    require(document.begin("Tune light")&&document.update(tune)&&document.commit(),document.status());
    require(document.history().size()==commands+1,"One light gesture created multiple commands.");
    const auto changedPoint=*document.scene().find(point),changedSun=*document.scene().find(sun);
    require(changedPoint.point->color==originalPoint.point->color&&changedSun.directional->color==originalSun.directional->color,"Temperature editing overwrote manual RGB.");
    require(document.undo()&&sameEntity(originalPoint,*document.scene().find(point))&&sameEntity(originalSun,*document.scene().find(sun)),"Light undo lost original modes or settings.");
    require(document.redo()&&sameEntity(changedPoint,*document.scene().find(point)),"Light redo lost temperature or flickering.");
    require(document.begin("Cancel light")&&document.update([](auto& records,auto&){for(auto& record:records)if(record.point){record.point->temperatureKelvin=1000;record.point->flicker.enabled=false;}}),document.status());
    document.cancel();require(sameEntity(changedPoint,*document.scene().find(point)),"Canceled light gesture changed saved settings.");
    require(document.save(),document.status());const auto file=project.root/document.assetId();const auto bytes=read(file);
    require(Json::parse(bytes).at("version")==25,"Optional light authoring did not retain the current scene version.");
    SceneDocument loaded(project.root);require(loaded.load(document.assetId())&&!loaded.dirty()&&read(file)==bytes,"Loading light settings wrote or dirtied a scene.");
    require(sameEntity(changedPoint,*loaded.scene().find(point))&&sameEntity(changedSun,*loaded.scene().find(sun)),"Not all light properties round-tripped.");
    loaded.select({point},point);require(loaded.duplicateSelection(),loaded.status());const auto copy=loaded.scene().find(loaded.selection().primary);
    require(copy&&copy->id!=point&&copy->point==changedPoint.point,"Duplicating a light lost temperature/flicker tuning.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(loaded)&&!runtime.dirty(),runtime.status());auto runtimeLight=*runtime.scene().find(point);
    runtimeLight.point->temperatureKelvin=12000;runtimeLight.point->flicker.seed=42;
    require(runtime.updateEntity(point,runtimeLight)&&sameEntity(changedPoint,*loaded.scene().find(point))&&!runtime.save(),"Play light edits escaped into authored data.");
}
void missingFieldsAndStrictValidation(){
    Project project;SceneDocument document(project.root);const auto id=document.createPointLight("Strict light");
    auto record=*document.scene().find(id);record.point->colorMode=LightColorMode::Temperature;record.point->temperatureKelvin=3200;record.point->flicker=authoredFlicker();
    require(document.updateEntity(id,record)&&document.save("Strict lights"),document.status());
    const auto file=project.root/document.assetId();const auto valid=Json::parse(read(file));
    const auto malformed=[&](const auto& change){auto data=valid;for(auto& entity:data["entities"])if(entity.contains("point"))change(entity["point"]);
        writeFileAtomically(file,data.dump());SceneDocument loaded(project.root);return !loaded.load(document.assetId());};
    for(const auto& value:Json::array({true,1,nullptr,"Kelvin"}))require(malformed([&](auto& light){light["colorMode"]=value;}),"Malformed color mode was accepted.");
    for(const auto& value:Json::array({true,"6500",nullptr,999,15001}))require(malformed([&](auto& light){light["temperatureKelvin"]=value;}),"Malformed temperature was accepted.");
    for(const auto& value:Json::array({false,1,nullptr}))require(malformed([&](auto& light){light["flicker"]=value;}),"Nonobject flicker was accepted.");
    require(malformed([](auto& light){light["flicker"]["enabled"]=1;}),"Nonboolean flicker switch was accepted.");
    require(malformed([](auto& light){light["flicker"]["behavior"]="unknown";}),"Unknown flicker behavior was accepted.");
    require(malformed([](auto& light){light["flicker"]["playback"]="forever";}),"Unknown flicker playback was accepted.");
    for(const auto& value:Json::array({-1,1.25,4294967296ULL}))require(malformed([&](auto& light){light["flicker"]["seed"]=value;}),"Malformed seed was accepted.");
    require(malformed([](auto& light){light["flicker"]["color"]["enabled"]="true";}),"Nonboolean channel switch was accepted.");
    require(malformed([](auto& light){light["flicker"]["intensity"]["minimum"]=5;light["flicker"]["intensity"]["maximum"]=1;}),"Inverted multiplier range was accepted.");
    require(malformed([](auto& light){light["flicker"]["flashesMaximum"]=100000;}),"Unbounded flash count was accepted.");
    require(malformed([](auto& light){light["flicker"]["radius"]=true;}),"Nonobject radius channel was accepted.");
    auto legacy=valid;for(auto& entity:legacy["entities"])if(entity.contains("point")){auto& light=entity["point"];light.erase("colorMode");light.erase("temperatureKelvin");light.erase("flicker");}
    writeFileAtomically(file,legacy.dump());const auto bytes=read(file);SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&!loaded.dirty()&&loaded.scene().find(id)->point->colorMode==LightColorMode::Color&&
        loaded.scene().find(id)->point->temperatureKelvin==6500&&loaded.scene().find(id)->point->flicker==LightFlickerSettings{}&&read(file)==bytes,"Legacy light loading changed appearance or wrote defaults.");
    const auto unchanged=snapshot(loaded);
    const auto rejected=[&](const auto& mutate){auto candidate=*loaded.scene().find(id);mutate(*candidate.point);return !loaded.updateEntity(id,candidate)&&snapshot(loaded)==unchanged;};
    require(rejected([](auto& light){light.colorMode=static_cast<LightColorMode>(99);}),"API accepted invalid color mode.");
    require(rejected([](auto& light){light.temperatureKelvin=std::numeric_limits<float>::quiet_NaN();}),"API accepted nonfinite Kelvin.");
    require(rejected([](auto& light){light.flicker.period=0;}),"API accepted zero period.");
    require(rejected([](auto& light){light.flicker.color.minimumTint={1,-1,1};}),"API accepted invalid tint.");
}

void spotLifecycleAndValidation(){
    Project project;SceneDocument document(project.root);
    const auto id=document.createSpotLight();require(id&&document.save("Spot lights"),document.status());
    const auto initial=*document.scene().find(id);require(initial.spot&&!initial.point&&hasSceneComponent(initial,SceneComponentKind::Spot),"Spot creation did not retain its independent component identity.");
    require(initial.spot->innerAngle==30&&initial.spot->outerAngle==45&&initial.spot->coneFalloff==1,"Spot half-cone defaults changed.");
    const auto commands=document.history().size();auto tuned=initial;
    tuned.spot->innerAngle=12;tuned.spot->outerAngle=67;tuned.spot->coneFalloff=2.5f;tuned.spot->radius=14;
    tuned.spot->color={.2f,.5f,.7f};tuned.spot->colorMode=LightColorMode::Temperature;tuned.spot->temperatureKelvin=3200;
    tuned.spot->flicker=authoredFlicker();tuned.spot->castShadows=true;tuned.spot->shadowDepthBias=.012f;tuned.spot->shadowNormalBias=2;
    tuned.spot->roles={lighting_scope::Character,lighting_scope::World};tuned.spot->indirectEnergy=2.75f;
    require(document.begin("Tune spot cone")&&document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==id)record=tuned;})&&document.commit(),document.status());
    require(document.history().size()==commands+1&&document.undo()&&sameEntity(initial,*document.scene().find(id))&&document.redo()&&sameEntity(tuned,*document.scene().find(id)),"Spot gesture undo/redo did not retain exact light settings.");
    require(document.begin("Cancel spot")&&document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==id){record.spot->innerAngle=0;record.spot->outerAngle=89;record.spot->flicker.enabled=false;}}),document.status());
    document.cancel();require(sameEntity(tuned,*document.scene().find(id)),"Canceled spot edits escaped their transaction.");
    require(document.save(),document.status());const auto file=project.root/document.assetId();const auto bytes=read(file);const auto valid=Json::parse(bytes);
    SceneDocument loaded(project.root);require(loaded.load(document.assetId())&&!loaded.dirty()&&read(file)==bytes&&sameEntity(tuned,*loaded.scene().find(id)),"Spot loading wrote defaults or lost cone/point authoring.");
    loaded.select({id},id);require(loaded.duplicateSelection(),loaded.status());const auto duplicate=loaded.scene().find(loaded.selection().primary);
    require(duplicate&&duplicate->id!=id&&duplicate->spot==tuned.spot,"Spot duplication lost its authored channels.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(loaded),runtime.status());auto changed=*runtime.scene().find(id);changed.spot->outerAngle=80;changed.spot->flicker.seed=42;
    require(runtime.updateEntity(id,changed)&&sameEntity(tuned,*loaded.scene().find(id))&&!runtime.save(),"Spot Play edits changed the authored scene.");
    const auto unchanged=snapshot(loaded);const auto rejected=[&](const auto& mutate){auto candidate=*loaded.scene().find(id);mutate(*candidate.spot);return !loaded.updateEntity(id,candidate)&&snapshot(loaded)==unchanged;};
    for(float bad:{-.1f,90.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        require(rejected([&](auto& light){light.outerAngle=bad;}),"Spot accepted an invalid outer half-cone.");
    require(rejected([](auto& light){light.innerAngle=-1;})&&rejected([](auto& light){light.innerAngle=68;}),"Spot accepted an inverted or negative inner cone.");
    require(rejected([](auto& light){light.coneFalloff=0;})&&rejected([](auto& light){light.coneFalloff=17;}),"Spot accepted unbounded cone falloff.");
    require(rejected([](auto& light){light.radius=0;})&&rejected([](auto& light){light.shadowDepthBias=-1;})&&rejected([](auto& light){light.roles.receiverMask=128;}),"Spot skipped shared point/shadow/scope validation.");
    auto ambiguous=*loaded.scene().find(id);ambiguous.point.emplace();
    require(!loaded.updateEntity(id,ambiguous)&&snapshot(loaded)==unchanged,"An entity accepted two local-light kinds sharing a shadow identity.");
    const auto malformed=[&](const auto& change){auto data=valid;for(auto& entity:data["entities"])if(entity.contains("spot"))change(entity["spot"]);
        writeFileAtomically(file,data.dump());SceneDocument candidate(project.root);return !candidate.load(document.assetId());};
    for(const auto* field:{"innerAngle","outerAngle","coneFalloff"})for(const auto& bad:Json::array({true,"45",nullptr}))
        require(malformed([&](auto& light){light[field]=bad;}),"Spot accepted a nonnumeric cone property.");
    require(malformed([](auto& light){light["innerAngle"]=70;light["outerAngle"]=40;}),"Spot file accepted an inverted cone.");
    require(malformed([](auto& light){light["flicker"]["enabled"]=1;}),"Spot file skipped strict flicker validation.");
    auto ambiguousFile=valid;for(auto& entity:ambiguousFile["entities"])if(entity.contains("spot"))entity["point"]=entity["spot"];
    writeFileAtomically(file,ambiguousFile.dump());SceneDocument ambiguousDocument(project.root);
    require(!ambiguousDocument.load(document.assetId()),"Scene file accepted point and spot components with the same local-light identity.");
    auto missing=valid;for(auto& entity:missing["entities"])entity.erase("spot");writeFileAtomically(file,missing.dump());const auto missingBytes=read(file);
    SceneDocument ordinary(project.root);require(ordinary.load(document.assetId())&&!ordinary.dirty()&&!ordinary.scene().find(id)->spot&&read(file)==missingBytes,"An older scene acquired a spot component or was rewritten on load.");
}
void spotExtraction(){
    EntityRecord parent;parent.id=1;parent.name="Affine parent";parent.local=math::translation({3,2,1})*math::eulerRotationXYZ({.1f,.7f,-.2f});
    EntityRecord spot;spot.id=2;spot.parent=1;spot.name="Spot";spot.local=math::translation({0,4,-2})*math::scaling({-2,3,.5f});spot.local[8]+=.2f;spot.spot.emplace();
    spot.spot->innerAngle=15;spot.spot->outerAngle=70;spot.spot->coneFalloff=3;spot.spot->radius=14;spot.spot->castShadows=true;
    spot.spot->nearFalloffRadius=1;spot.spot->sourceRadius=.2f;spot.spot->roles={lighting_scope::Character,lighting_scope::World};spot.spot->indirectEnergy=3;
    spot.spot->colorMode=LightColorMode::Temperature;spot.spot->temperatureKelvin=3200;spot.spot->flicker=authoredFlicker();
    const auto authored=spot;Scene scene;scene.apply({spot,parent},{});const auto world=parent.local*spot.local;
    const auto extracted=extractScene(scene,{},1.25);require(extracted.points.size()==1&&extracted.directional.empty(),"Spot transport duplicated or lost an independent local light.");
    const auto& light=extracted.points.front();const auto expected=evaluateLightPresentation(spot.spot->colorMode,spot.spot->color,spot.spot->temperatureKelvin,spot.spot->intensity,spot.spot->radius,spot.spot->flicker,1.25,true);
    const Vec3 negativeWorldZ{-world[8],-world[9],-world[10]};
    const auto expectedDirection=negativeWorldZ/static_cast<float>(std::sqrt(math::dot(negativeWorldZ,negativeWorldZ)));
    require(light.type==LocalLightType::Spot&&light.entityId==2&&math::length(light.position-math::transformPoint(world,{}))<1.e-5f&&
        math::length(light.direction-expectedDirection)<1.e-5f,"Spot position/direction ignored affine parenting or local -Z.");
    require(light.color==expected.color&&light.intensity==expected.intensity&&light.radius==expected.radius&&light.innerConeAngle==15&&light.outerConeAngle==70&&light.coneFalloff==3,
        "Spot extraction did not share point flicker or preserve its world-space range and half-cone angles.");
    require(light.castShadows&&light.roles==spot.spot->roles&&light.indirectEnergy==3&&light.nearFalloffRadius==1&&light.sourceRadius==.2f&&sameEntity(authored,*scene.find(2)),
        "Spot extraction altered authored channels or lost shadow/scope response.");
    const auto base=extractScene(scene);require(base.points.front().radius==14&&base.points.front().intensity==35&&base.points.front().color==temperatureColorSrgb(3200),"Static spot extraction retained flicker or transformed range by entity scale.");
    parent.visible=false;scene.apply({spot,parent},{});require(extractScene(scene).points.empty(),"A hidden spot ancestor continued illuminating the scene.");

}
}
int main(){try{persistenceHistoryAndIsolation();missingFieldsAndStrictValidation();spotLifecycleAndValidation();spotExtraction();std::cout<<"Light scene persistence, validation, history, duplication, Play isolation and source sync passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
