#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
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
void sourceLightOwnership(){
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    EntityRecord point;point.id=1;point.name="Source point";point.point.emplace();point.quaderGenerated=QuaderGenerated{owner,"light/point"};
    EntityRecord sun;sun.id=2;sun.name="Source sun";sun.directional.emplace();sun.quaderGenerated=QuaderGenerated{owner,"light/sun"};
    require(document.replaceQuaderImport(owner,link,{point,sun}),document.status());
    EntityId pointId{},sunId{};for(const auto& value:document.scene().records())if(value.quaderGenerated){if(value.quaderGenerated->sourceKey=="light/point")pointId=value.id;if(value.quaderGenerated->sourceKey=="light/sun")sunId=value.id;}
    auto p=*document.scene().find(pointId),s=*document.scene().find(sunId);p.point->colorMode=LightColorMode::Temperature;p.point->temperatureKelvin=2700;p.point->flicker=authoredFlicker();
    s.directional->colorMode=LightColorMode::Temperature;s.directional->temperatureKelvin=8000;s.directional->flicker=authoredFlicker();
    require(document.updateEntity(pointId,p)&&document.updateEntity(sunId,s),document.status());const auto before=snapshot(document);
    point.point->color={.2f,.4f,.8f};point.point->radius=14;point.point->intensity=45;sun.directional->color={.8f,.6f,.3f};sun.directional->intensity=3;
    require(document.replaceQuaderImport(owner,link,{point,sun}),document.status());
    const auto nextP=document.scene().find(pointId)->point;
    const auto nextS=document.scene().find(sunId)->directional;
    require(nextP->colorMode==LightColorMode::Color&&nextP->color==point.point->color&&nextP->radius==14&&nextP->temperatureKelvin==2700&&nextP->flicker==p.point->flicker,"Source sync lost point flicker or did not restore source RGB ownership.");
    require(nextS->colorMode==LightColorMode::Color&&nextS->color==sun.directional->color&&nextS->temperatureKelvin==8000&&nextS->flicker==s.directional->flicker,"Source sync lost directional flicker or temperature memory.");
    const auto after=snapshot(document);require(document.undo()&&snapshot(document)==before&&document.redo()&&snapshot(document)==after,"Source light refresh did not preserve atomic history.");
}
}
int main(){try{persistenceHistoryAndIsolation();missingFieldsAndStrictValidation();sourceLightOwnership();std::cout<<"Light scene persistence, validation, history, duplication, Play isolation and source sync passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
