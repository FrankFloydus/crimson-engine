#include "scene/SceneDocument.h"
#include "scene/EnvironmentSerialization.h"
#include "editor/ViewportSelection.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "resources/Paths.h"
#include "resources/AssetId.h"
#include "math/Json.h"

#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
using namespace crimson;
using Json = nlohmann::json;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void nearMatrix(const Mat4& a, const Mat4& b, float tolerance = 0.0001f) {
    for (size_t i=0;i<16;++i)
        require(std::isfinite(a[i]) && std::abs(a[i]-b[i]) <= tolerance,"Matrix changed unexpectedly at " + std::to_string(i));
}
template<class Action> void rejects(Action&& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"Invalid scene mutation was accepted.");
}
struct TempProject {
    std::filesystem::path root;
    TempProject() {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        static unsigned sequence{};
        root = std::filesystem::temp_directory_path() / ("crimson-scene-test-" + std::to_string(ticks) + "-" + std::to_string(++sequence));
        std::filesystem::create_directories(root);
    }
    ~TempProject() {
        std::error_code ignored;
        const auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ignored);
        const auto target = std::filesystem::weakly_canonical(root,ignored);
        if (!ignored && target.parent_path() == parent && target.filename().string().starts_with("crimson-scene-test-"))
            std::filesystem::remove_all(target,ignored);
    }
};
std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void writeText(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path,std::ios::binary); output << value;
    require(static_cast<bool>(output),"Cannot write test fixture.");
}
void sameRecords(const std::vector<EntityRecord>& a, const std::vector<EntityRecord>& b) {
    require(a.size() == b.size(),"Entity count differs.");
    for (size_t i=0;i<a.size();++i) require(sameEntity(a[i],b[i]),"Entity data differs.");
}

void physicalSceneStorage() {
    TempProject project;SceneDocument document(project.root);
    const auto entity=document.createEmpty("Preserved entity");
    require(document.save("Migrated scene"),document.status());
    const auto legacyId=document.assetId();const auto original=project.root/pathFromUtf8(legacyId);
    initializeContentStorage(project.root);
    const auto migrated=resolveAssetPath(project.root,legacyId);
    const auto id=readSceneSnapshot(project.root,legacyId).id;
    require(isAssetUuid(id)&&id==canonicalAssetId(legacyId)&&document.load(legacyId)&&document.scene().find(entity)&&!document.externalFileChanged(),document.status());
    require(migrated!=original&&!std::filesystem::exists(original),"Physical scene migration retained the legacy file location.");
    const auto moved=project.root/"Content/Levels/Act One/Opening.crimson-scene";
    const std::array<ContentPathMove,1> moves{{{migrated,moved,false}}};
    relocateContentPaths(project.root,moves);
    require(resolveAssetPath(project.root,id)==std::filesystem::weakly_canonical(moved)&&assetIdAtPath(project.root,moved)==id,
        "Scene move failed to retain its original logical ID.");
    require(document.edit("Edit after physical move",[&](auto& records,auto&){for(auto& record:records)if(record.id==entity)record.name="Edited entity";})&&document.save(),document.status());
    const auto saved=readSceneSnapshot(project.root,id);
    require(saved.id==id&&saved.records.size()==1&&saved.records.front().name=="Edited entity"&&!std::filesystem::exists(original),
        "Saving a relocated scene recreated its legacy path or lost entity data.");
    const auto catalog=scanSceneAssets(project.root);
    require(catalog.size()==1&&catalog.front().id==id&&catalog.front().name=="Migrated scene",
        "Recursive indexed scene discovery missed or duplicated a moved scene.");
    SceneDocument reopened(project.root);require(reopened.load(id)&&reopened.scene().find(entity),reopened.status());
    reopened.newScene();require(reopened.save("New Content scene"),reopened.status());
    require(std::filesystem::is_regular_file(resolveAssetPath(project.root,reopened.assetId()))&&scanSceneAssets(project.root).size()==2,
        "A newly saved Content scene was not registered for indexed discovery.");
}

void registryAndHierarchy() {
    Scene scene;
    EntityRecord root; root.id=1; root.name="Root";
    root.local=math::translation({3,2,-5}) * math::rotation(math::axisAngle({0,1,0},0.5)) * math::scaling({-2,3,0.5});
    EntityRecord child; child.id=2; child.name="Child"; child.parent=1;
    child.local=math::translation({1,2,3}); child.local[4]=0.3f;
    child.model=AssetRef<ModelAsset>{"assets/models/missing/manifest.json"};
    EntityRecord light; light.id=3; light.name="Light"; light.order=1; light.point.emplace();
    scene.apply({child,light,root},{});
    require(scene.registry().view<const Identity>().size() == 3,"EnTT does not own all entities.");
    require(scene.preorder() == std::vector<EntityId>({1,2,3}),"Hierarchy order is not deterministic.");
    require(scene.children(1) == std::vector<EntityId>({2}),"Child lookup failed.");
    require(scene.subtree(1) == std::vector<EntityId>({1,2}),"Subtree lookup failed.");
    require(scene.topmost({2,1,3,3,999}) == std::vector<EntityId>({1,3}),"Selected descendants were not filtered.");
    nearMatrix(*scene.worldTransform(2),root.local*child.local);
    require(!scene.find(999) && !scene.worldTransform(999),"Absent entity resolves unexpectedly.");
    const auto records = scene.records(); const auto generation = scene.generation(); const auto revision = scene.revision();
    auto malformed=records; malformed[0].parent=2;
    rejects([&] { scene.apply(malformed,{}); });
    sameRecords(scene.records(),records);
    require(scene.generation()==generation && scene.revision()==revision,"Rejected mutation changed scene generation.");
    malformed=records; malformed[0].parent=999;
    rejects([&] { scene.apply(malformed,{}); });
    malformed=records; malformed.push_back(malformed.front());
    rejects([&] { scene.apply(malformed,{}); });
    malformed=records; malformed[0].local[0]=std::numeric_limits<float>::infinity();
    rejects([&] { scene.apply(malformed,{}); });
    malformed=records; malformed[0].local[3]=1;
    rejects([&] { scene.apply(malformed,{}); });
    malformed=records; malformed[1].model=AssetRef<ModelAsset>{"../outside.glb"};
    rejects([&] { scene.apply(malformed,{}); });
    scene.apply(records,{});
    require(scene.generation()!=generation && scene.revision()==revision+1,"Registry replacement did not invalidate old handles.");
}

void reparentPreservesAffine() {
    TempProject project; SceneDocument document(project.root);
    const auto parent=document.createEmpty("Rotated scaled parent");
    auto record=*document.scene().find(parent);
    record.local=math::translation({4,1,-2}) * math::rotation(math::axisAngle({0,1,0},0.7)) * math::scaling({-2,3,0.5});
    require(document.updateEntity(parent,record),document.status());
    const auto child=document.createModel("assets/models/one/manifest.json","Child",math::translation({2,3,4}));
    const auto grandchild=document.createEmpty("Grandchild");
    document.select({grandchild}); require(document.reparentSelection(child),document.status());
    document.select({child,grandchild},child);
    const auto beforeChild=*document.scene().worldTransform(child);
    const auto beforeGrandchild=*document.scene().worldTransform(grandchild);
    const auto beforeRecords=document.scene().records();
    require(document.reparentSelection(parent),document.status());
    nearMatrix(*document.scene().worldTransform(child),beforeChild);
    nearMatrix(*document.scene().worldTransform(grandchild),beforeGrandchild);
    require(document.scene().find(grandchild)->parent==child,"Reparent detached selected descendant.");
    require(document.undo(),"Reparent undo failed."); sameRecords(document.scene().records(),beforeRecords);
    require(document.redo(),"Reparent redo failed.");
    document.select({parent});
    const auto current=document.scene().records(); const auto history=document.history().size();
    require(!document.reparentSelection(grandchild),"Cycle reparent accepted.");
    sameRecords(document.scene().records(),current);
    require(document.history().size()==history,"Rejected reparent created history.");
    const auto singular=document.createEmpty("Singular");
    record=*document.scene().find(singular); record.local=math::scaling({0,1,1});
    require(document.updateEntity(singular,record),document.status());
    document.select({child}); require(!document.reparentSelection(singular),"Singular parent accepted.");
    require(document.reparentSelection(NoEntity),document.status());
    nearMatrix(*document.scene().worldTransform(child),beforeChild);
}

void transactionsAndSelection() {
    TempProject project; SceneDocument document(project.root);
    require(document.dirty(),"New unsaved scene should be dirty.");
    const auto parent=document.createEmpty("Group");
    const auto child=document.createModel("assets/models/a/manifest.json","Child",math::translation({2,1,3}));
    document.select({child}); require(document.reparentSelection(parent),document.status());
    const auto other=document.createPointLight();
    document.select({parent,child,other,parent,999},other);
    require(document.selection().ids.size()==3 && document.selection().primary==other,"Selection sanitation failed.");
    require(document.save("Transaction scene"),document.status());
    const auto before=document.scene().records(); const auto count=document.history().size();
    const auto roots=document.scene().topmost(document.selection().ids);
    std::unordered_map<EntityId,Mat4> originals;
    for (auto id : roots) originals.emplace(id,*document.scene().worldTransform(id));
    const auto beforeChild=*document.scene().worldTransform(child);
    require(document.begin("Move selection"),document.status());
    document.select({child}); require(document.selection().ids.size()==3,"Selection changed during transaction.");
    require(!document.undo(),"Undo ran during transaction.");
    for (int frame=1;frame<=12;++frame) {
        const Mat4 delta=math::translation({static_cast<float>(frame),0,0});
        require(document.update([&](auto& records,auto&) {
            for (auto& record : records) if (originals.contains(record.id)) record.local=delta*originals.at(record.id);
        }),document.status());
    }
    require(document.dirty(),"Pending changes did not mark scene dirty.");
    require(document.history().size()==count,"Drag added commands before release.");
    document.cancel(); sameRecords(document.scene().records(),before);
    require(!document.dirty() && document.history().size()==count,"Canceled drag changed history/dirty state.");
    require(document.begin("Move selection"),document.status());
    const Mat4 delta=math::translation({4,0,0});
    require(document.update([&](auto& records,auto&) {
        for (auto& record : records) if (originals.contains(record.id)) record.local=delta*originals.at(record.id);
    }),document.status());
    require(document.commit(),document.status());
    nearMatrix(*document.scene().worldTransform(child),delta*beforeChild);
    require(document.history().size()==count+1,"Drag did not coalesce to one command.");
    require(document.undo(),"Move undo failed."); sameRecords(document.scene().records(),before);
    require(!document.dirty(),"Undo to saved state remained dirty.");
    require(document.begin("No operation") && document.commit(),"No-op transaction failed.");
    require(document.history().canRedo(),"No-op discarded redo.");
    require(!document.edit("Invalid",[](auto& records,auto&) { records[0].parent=records[0].id; }),"Invalid transaction accepted.");
    require(document.history().canRedo(),"Rejected transaction discarded redo.");
    sameRecords(document.scene().records(),before);
    require(document.redo(),"Redo after no-op failed.");
}

void viewportParentSelection() {
    TempProject project; SceneDocument document(project.root);
    const auto parent=document.createEmpty("Object");
    const auto branch=document.createEmpty("Visual branch");
    document.select({branch});require(document.reparentSelection(parent),document.status());
    const auto model=document.createModel("assets/models/viewport/manifest.json","Model");
    document.select({model});require(document.reparentSelection(branch),document.status());
    const auto collider=document.createCollider("Collision",model);
    const auto particle=document.createParticle("assets/particles/viewport.crimson-particle","Particles");
    const auto light=document.createPointLight();
    const auto camera=document.createCamera();
    document.select({particle,light,camera});require(document.reparentSelection(branch),document.status());
    const auto other=document.createEmpty("Other object");
    const auto character=document.createCharacter();
    document.select({character});require(document.reparentSelection(other),document.status());
    require(parent && branch && model && collider && particle && light && camera && other && character,"Viewport selection fixture failed.");
    require(viewportSelectionRoot(document.scene(),NoEntity)==NoEntity && viewportSelectionRoot(document.scene(),999)==NoEntity,
        "Empty or stale viewport hits resolved to a selection.");
    const auto history=document.history().size();
    for(const auto hit:std::array<EntityId,7>{parent,branch,model,collider,particle,light,camera}) {
        require(selectViewportHit(document,hit,false,false),"Viewport selection was blocked.");
        require(document.selection().ids==std::vector<EntityId>{parent} && document.selection().primary==parent,
            "A viewport model, collision or marker hit selected a child instead of the top-level parent.");
    }
    require(selectViewportHit(document,character,false,true) && document.selection().ids==std::vector<EntityId>({parent,other}) &&
        document.selection().primary==other,"Shift-click did not append the hit's top-level parent.");
    require(selectViewportHit(document,collider,false,true) && document.selection().ids==std::vector<EntityId>({parent,other}) &&
        document.selection().primary==parent,"Shift-click duplicated an already selected parent.");
    require(selectViewportHit(document,model,true,false) && document.selection().ids==std::vector<EntityId>{other},
        "Ctrl-click did not toggle the parent off through its child.");
    require(selectViewportHit(document,particle,true,true) && document.selection().ids==std::vector<EntityId>({other,parent}),
        "Ctrl+Shift-click did not retain toggle behavior.");
    require(selectViewportHit(document,NoEntity,false,true) && document.selection().ids==std::vector<EntityId>({other,parent}),
        "Shift-clicking the background cleared selection.");
    require(selectViewportHit(document,NoEntity,false,false) && document.selection().ids.empty(),"Background click did not clear selection.");
    document.select({collider},collider);
    require(document.selection().primary==collider && document.selection().ids==std::vector<EntityId>{collider},
        "Explicit Outliner-style selection no longer addresses a child.");
    require(document.begin("Active transform"),document.status());
    require(!selectViewportHit(document,model,false,false) && document.selection().primary==collider,
        "Viewport selection changed during an active document edit.");
    document.cancel();
    require(document.history().size()==history,"Viewport selection added authored history.");
}

void subtreeCommands() {
    TempProject project; SceneDocument document(project.root);
    const auto parent=document.createEmpty("Parent");
    const auto child=document.createModel("assets/models/a/manifest.json","Model",math::scaling({-1,2,3}));
    document.select({child}); require(document.reparentSelection(parent),document.status());
    const auto light=document.createDirectionalLight();
    document.select({light}); require(document.reparentSelection(child),document.status());
    document.select({parent,child},child);
    const auto before=document.scene().records(); const auto count=document.history().size();
    require(document.duplicateSelection(),document.status());
    require(document.scene().records().size()==6,"Duplicate double-copied selected descendants.");
    require(document.selection().ids.size()==2 && document.selection().primary!=child,"Duplicate selection was not remapped.");
    const auto duplicateIds=document.selection(); const auto after=document.scene().records();
    require(document.history().size()==count+1,"Duplicate was not one command.");
    require(document.undo(),"Duplicate undo failed."); sameRecords(document.scene().records(),before);
    require(document.selection().primary==child,"Duplicate undo did not restore selection.");
    require(document.redo(),"Duplicate redo failed."); sameRecords(document.scene().records(),after);
    require(document.selection().ids==duplicateIds.ids,"Redo allocated different entity IDs.");
    require(document.eraseSelection(),document.status());
    sameRecords(document.scene().records(),before);
    require(document.selection().ids.empty(),"Delete left stale selection IDs.");
    require(document.undo(),"Delete undo failed."); sameRecords(document.scene().records(),after);
    require(document.selection().ids==duplicateIds.ids,"Delete undo failed to restore selection.");
}

void persistenceAndCatalog() {
    TempProject project;
    const std::vector<std::filesystem::path> preserved={"assets/material.json","assets/environment.json","assets/preview.json","crimson_ui.ini"};
    for (const auto& path : preserved) writeText(project.root/path,"preserve legacy data\n");
    SceneDocument document(project.root);
    const auto id=document.createModel("assets/models/missing/manifest.json","Missing model",math::translation({1,2,3}));
    document.createPointLight(); document.createDirectionalLight();
    const auto environment=document.createEnvironment(); auto environmentRecord=*document.scene().find(environment);
    environmentRecord.environment->environment.map="assets/textures/missing.hdr";
    environmentRecord.environment->environment.rotation=35; environmentRecord.environment->environment.intensity=2;
    environmentRecord.environment->exposure=1.7f;
    require(document.updateEntity(environment,environmentRecord),document.status());
    const auto settings=document.scene().properties();
    const auto records=document.scene().records();
    require(document.save("Shared name"),document.status());
    const auto firstId=document.assetId();
    require(!document.dirty() && !firstId.empty(),"Save did not mark document clean.");
    const auto firstBytes=readText(project.root/firstId);const auto savedHistory=document.history().size();
    require(!document.save("Shared name",true),"Save As created an ambiguous duplicate scene name.");
    require(!document.save(" SHARED NAME ",true),"Case/whitespace variant bypassed duplicate scene protection.");
    require(document.assetId()==firstId&&!document.dirty(),"Rejected Save As changed the current scene.");
    require(document.displayName()=="Shared name"&&document.history().size()==savedHistory&&
        readText(project.root/firstId)==firstBytes&&scanSceneAssets(project.root).size()==1,"Rejected Save As rewrote saved data.");
    SceneDocument unsaved(project.root);unsaved.createEmpty("Retain unsaved work");
    const auto unsavedRecords=unsaved.scene().records();const auto unsavedHistory=unsaved.history().size();
    require(!unsaved.save(" shared NAME ")&&unsaved.assetId().empty()&&unsaved.dirty()&&unsaved.history().size()==unsavedHistory,
        "A new scene bypassed duplicate-name protection or lost its history.");
    sameRecords(unsaved.scene().records(),unsavedRecords);
    require(availableSceneName(project.root,"Shared name")=="Shared name 2","New scene name suggestion collides.");
    require(document.save("Shared name Copy",true),document.status());
    const auto secondId=document.assetId();
    require(firstId!=secondId,"Save As reused the existing asset ID.");
    require(availableSceneName(project.root,"Shared name Copy")=="Shared name Copy 2","Repeated Save As name collides.");
    require(!document.save("Shared name"),"Scene rename created a duplicate name.");
    // Legacy projects may already contain duplicate names. Preserve both IDs
    // and allow ordinary saves rather than rewriting or merging their data.
    auto legacy=readSceneSnapshot(project.root,secondId);legacy.name="Shared name";
    saveSceneSnapshot(project.root,secondId,legacy);
    require(document.load(secondId)&&document.save(),document.status());
    require(readText(project.root/firstId)==firstBytes,"Saving a legacy duplicate rewrote the other scene.");
    auto catalog=scanSceneAssets(project.root);
    require(catalog.size()==2 && catalog[0].name=="Shared name" && catalog[1].name=="Shared name","Catalog lost colliding scene names.");
    SceneDocument reopened(project.root);
    require(reopened.load(firstId),reopened.status());
    sameRecords(reopened.scene().records(),records);
    require(sameProperties(reopened.scene().properties(),settings),"Scene settings did not round-trip.");
    require(reopened.scene().find(id)->model==std::optional<AssetRef<ModelAsset>>(AssetRef<ModelAsset>{"assets/models/missing/manifest.json"}),"Missing model reference was erased.");
    require(!reopened.history().canUndo() && !reopened.dirty(),"Reopened scene has stale history.");
    reopened.createEmpty("Unsaved");
    const auto current=reopened.scene().records(); const auto count=reopened.history().size();
    auto corrupt=Json::parse(readText(project.root/firstId)); corrupt["version"]=999;
    writeText(project.root/"assets/scenes/corrupt.crimson-scene",corrupt.dump());
    require(!reopened.load("assets/scenes/corrupt.crimson-scene"),"Unsupported scene version loaded.");
    sameRecords(reopened.scene().records(),current);
    require(reopened.history().size()==count && reopened.dirty(),"Failed load changed history or dirty state.");
    require(!reopened.load("../outside.crimson-scene"),"Scene traversal accepted.");
    std::vector<std::string> diagnostics;
    catalog=scanSceneAssets(project.root,&diagnostics);
    require(catalog.size()==2 && diagnostics.size()==1,"Invalid scene catalog diagnostics missing.");
    std::string error;
    require(!deleteSceneAsset(project.root,"assets/material.json",error),"Scene deletion accepted another asset type.");
    require(!deleteSceneAsset(project.root,"assets/scenes/../material.crimson-scene",error),"Scene deletion escaped directory.");
    require(deleteSceneAsset(project.root,secondId,error),error);
    require(!std::filesystem::exists(project.root/secondId) && std::filesystem::exists(project.root/firstId),"Deleting a scene removed the wrong asset.");
    for (const auto& path : preserved) require(readText(project.root/path)=="preserve legacy data\n","Legacy project file changed.");
}

void dirtyTokensAndFailure() {
    TempProject project; SceneDocument document(project.root);
    const auto id=document.createEmpty("Initial");
    auto record=*document.scene().find(id); record.name="Saved state";
    require(document.updateEntity(id,record),document.status());
    require(document.save("Dirty markers"),document.status());
    require(document.undo() && document.dirty(),"Undo away from save did not mark dirty.");
    record=*document.scene().find(id); record.name="New branch";
    require(document.updateEntity(id,record),document.status());
    require(document.dirty() && !document.history().canRedo(),"New branch reused saved-state cursor identity.");
    require(document.save(),document.status());
    require(!document.dirty(),"Second save failed to mark clean.");
    require(document.undo() && document.dirty(),"Undo second save should be dirty.");
    require(document.redo() && !document.dirty(),"Redo to second save should be clean.");
    const auto savedId=document.assetId();
    require(!document.save(std::string(1025,'x'),true),"Invalid name was saved.");
    require(document.assetId()==savedId && !document.dirty(),"Failed Save As changed asset identity.");
    require(document.begin("Move before save"),document.status());
    require(document.update([&](auto& records,auto&) { records[0].local[12]=8; }),document.status());
    require(document.save(),document.status());
    require(!document.transactionActive() && !document.dirty(),"Saving did not finish active transaction.");
    require(document.undo() && document.dirty(),"Saved transaction is not undoable.");
    document.newScene();
    require(document.assetId().empty() && document.dirty() && document.scene().records().empty() && !document.history().canUndo(),"New scene retained document state.");
    TempProject blocked;
    writeText(blocked.root/"assets/scenes","directory is blocked");
    SceneDocument failure(blocked.root); failure.createEmpty();
    require(!failure.save("Cannot save") && failure.dirty() && failure.assetId().empty(),"Failed write changed dirty state/identity.");
    require(readText(blocked.root/"assets/scenes")=="directory is blocked","Failed save damaged existing data.");
}

void limitsAndHistoryBound() {
    TempProject project; SceneDocument document(project.root);
    for (size_t i=0;i<MaximumDirectionalLights;++i) require(document.createDirectionalLight()!=NoEntity,document.status());
    const auto count=document.history().size();
    require(document.createDirectionalLight()==NoEntity && document.history().size()==count,"Directional limit was not atomic.");
    EntityId point{};
    require(MaximumPointLights>=32,"Clustered lighting did not lift the legacy sixteen-light scene limit.");
    // Exercise normal edit/history/save behavior above the old ceiling without
    // rebuilding the whole scene for every one of thousands of point lights.
    for (size_t i=0;i<32;++i) { point=document.createPointLight(); require(point!=NoEntity,document.status()); }
    const auto records=document.scene().records();document.select({point});
    require(document.duplicateSelection(),document.status());
    require(document.undo(),"Point-light duplication above sixteen was not undoable.");
    sameRecords(document.scene().records(),records);
    auto invalid=*document.scene().find(point); invalid.point->intensity=-1;
    require(!document.updateEntity(point,invalid),"Negative intensity accepted.");
    require(document.save("Clustered point lights"),document.status());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    sameRecords(reopened.scene().records(),records);

    std::vector<EntityRecord> boundary;boundary.reserve(MaximumPointLights+1);
    for(size_t i=0;i<MaximumPointLights;++i) {
        EntityRecord light;light.id=i+1;light.name="Boundary light "+std::to_string(i);light.order=static_cast<uint32_t>(i);light.point.emplace();
        boundary.push_back(std::move(light));
    }
    Scene::validate(boundary,{});Scene bulk;bulk.apply(boundary,{});
    require(bulk.records().size()==MaximumPointLights,"Full point-light capacity failed bulk publication.");
    auto extra=boundary.back();extra.id=MaximumPointLights+1;extra.order=static_cast<uint32_t>(MaximumPointLights);boundary.push_back(extra);
    rejects([&]{Scene::validate(boundary,{});});rejects([&]{bulk.apply(boundary,{});});
    require(bulk.records().size()==MaximumPointLights && !bulk.contains(extra.id),"Rejected point-light overflow partially changed the scene.");
    boundary.pop_back();SceneDocument full(project.root);
    require(full.openPreparedSnapshot({"assets/scenes/full-clustered.crimson-scene","Full clustered scene",boundary,{},{}}),full.status());
    full.select({boundary.back().id});const auto fullHistory=full.history().size();
    require(!full.duplicateSelection() && full.history().size()==fullHistory,"Duplicating beyond point-light capacity was not atomic.");
    sameRecords(full.scene().records(),boundary);
    document.newScene(); const auto id=document.createEmpty();
    for (unsigned i=0;i<300;++i) {
        auto record=*document.scene().find(id); record.name="Edit " + std::to_string(i);
        require(document.updateEntity(id,record),document.status());
    }
    require(document.history().size()==UndoRedoManager::MaximumEntries,"History cap was not respected.");
    unsigned undos{}; while (document.undo()) ++undos;
    require(undos==UndoRedoManager::MaximumEntries,"History cap broke undo traversal.");
    unsigned redos{}; while (document.redo()) ++redos;
    require(redos==undos && document.scene().find(id)->name=="Edit 299","History cap broke redo state.");
}

void groupingAndBatchPlacement() {
    TempProject project; SceneDocument document(project.root);
    const auto parent=document.createEmpty("Parent");
    auto record=*document.scene().find(parent);
    record.local=math::translation({2,3,4})*math::rotation(math::axisAngle({0,1,0},.7))*math::scaling({-2,3,.5});
    require(document.updateEntity(parent,record),document.status());
    const auto placed=document.createModels({{"assets/models/a/manifest.json","A",math::translation({1,0,0})},
        {"assets/models/b/manifest.json","B",math::translation({-2,1,0})}});
    require(placed.size()==2 && document.selection().ids==placed,"Batch placement did not select its entities.");
    require(document.undo() && !document.scene().contains(placed[0]) && !document.scene().contains(placed[1]),"Batch drop was not one undo operation.");
    require(document.redo() && document.selection().ids==placed,"Batch redo lost stable IDs/selection.");
    require(document.reparentSelection(parent),document.status());
    const auto a=*document.scene().worldTransform(placed[0]), b=*document.scene().worldTransform(placed[1]);
    const auto count=document.history().size();
    require(document.groupSelection(),document.status());
    const auto group=document.selection().primary;
    require(group && document.scene().find(group)->parent==parent,"Grouping lost common parent.");
    nearMatrix(*document.scene().worldTransform(placed[0]),a);
    nearMatrix(*document.scene().worldTransform(placed[1]),b);
    require(document.history().size()==count+1,"Grouping split into multiple commands.");
    require(document.undo() && !document.scene().contains(group),"Undo grouping failed.");
    require(document.redo() && document.scene().contains(group),"Redo did not restore the group ID.");
    require(document.ungroupSelection(),document.status());
    require(!document.scene().contains(group) && document.scene().find(placed[0])->parent==parent,"Ungrouping did not promote children.");
    nearMatrix(*document.scene().worldTransform(placed[0]),a); nearMatrix(*document.scene().worldTransform(placed[1]),b);
    require(document.undo() && document.scene().contains(group),"Ungroup undo did not restore folder.");
    const auto before=document.scene().records(); const auto history=document.history().size();
    auto invalid=ModelPlacement{"../escaped/model.glb","Invalid",Mat4{}};
    require(document.createModels({{"assets/models/valid/manifest.json","Valid",Mat4{}},invalid}).empty(),"Malformed batch published partial entities.");
    sameRecords(document.scene().records(),before);
    require(document.history().size()==history,"Failed batch mutated history.");
}
void environmentsAndMigration() {
    TempProject project; SceneDocument document(project.root);
    require(document.scene().records().empty() && resolveEnvironment(document.scene()).environment.map.empty(),"New scenes must remain empty with the studio fallback.");
    const auto first=document.createEnvironment("Day"), second=document.createEnvironment("Night");
    require(first && second && document.scene().properties().activeEnvironment==first,"Creating a second environment changed activation.");
    auto night=*document.scene().find(second); night.environment->exposure=2; night.environment->environment.intensity=.25f;
    require(document.updateEntity(second,night) && document.setActiveEnvironment(second),document.status());
    require(resolveEnvironment(document.scene()).exposure==2,"Active environment extraction is wrong.");
    const auto before=document.history().size();
    require(document.setActiveEnvironment(second) && document.history().size()==before,"Unchanged activation entered history.");
    require(!document.setActiveEnvironment(999) && document.scene().properties().activeEnvironment==second,"Invalid activation was applied.");
    document.select({second}); require(document.duplicateSelection(),document.status());
    const auto duplicate=document.selection().primary;
    require(duplicate!=second && document.scene().find(duplicate)->environment && document.scene().properties().activeEnvironment==second,"Duplicating an environment stole activation.");
    const auto parent=document.createEmpty("Environment parent"); document.select({second});
    require(document.reparentSelection(parent),document.status()); document.select({parent});
    require(document.eraseSelection() && document.scene().properties().activeEnvironment==NoEntity,"Deleting an active environment's ancestor retained a dangling active ID.");
    require(resolveEnvironment(document.scene()).exposure==1,"No active environment did not use studio defaults.");
    require(document.undo() && document.scene().properties().activeEnvironment==second,"Undo did not restore activation and IDs.");
    require(document.redo() && document.scene().properties().activeEnvironment==NoEntity,"Redo did not restore default environment state.");
    document.select({duplicate}); require(!document.ungroupSelection(),"An Environment was treated as an empty group.");
    require(document.save("Environments"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    require(snapshot.properties.activeEnvironment==NoEntity && snapshot.records.size()==2,"Multi-environment scene round trip failed.");
    auto legacy=Json::parse(serializeSceneSnapshot(snapshot)); legacy["version"]=1;
    legacy["settings"]={{"exposure",1.7f},{"environment",{{"map","assets/textures/legacy.hdr"},{"enabled",true},{"intensity",3},{"rotation",42}}}};
    for (auto& entity:legacy["entities"]) entity.erase("environment");
    const auto oldId="assets/scenes/legacy.crimson-scene"; const auto contents=legacy.dump(2); writeText(project.root/oldId,contents);
    const auto migrated=readSceneSnapshot(project.root,oldId), repeated=readSceneSnapshot(project.root,oldId);
    require(migrated.properties.activeEnvironment==repeated.properties.activeEnvironment,"Legacy migration generated unstable IDs.");
    require(migrated.records.size()==snapshot.records.size()+1 && readText(project.root/oldId)==contents,"Legacy migration rewrote its source.");
    SceneDocument opened(project.root); require(opened.load(oldId),opened.status());
    require(!opened.dirty() && resolveEnvironment(opened.scene()).exposure==1.7f,"Legacy migration lost settings or marked an equivalent load dirty.");
    require(opened.save(),opened.status()); require(Json::parse(readText(project.root/oldId)).at("version")==12,"Explicit save did not write scene version 12.");
}
void environmentGlowHistoryAndMigration() {
    TempProject project;SceneDocument document(project.root);
    const auto id=document.createEnvironment("Glow");
    auto original=*document.scene().find(id);
    require(!original.environment->environment.glow.enabled,"New environments unexpectedly enabled Glow.");
    auto edited=original;edited.environment->environment.enabled=false;
    edited.environment->environment.glow={true,2.5f,3.25f,.75f};
    require(document.begin("Tune Glow"),document.status());
    const auto update=[&](auto& records,auto&){for(auto& record:records)if(record.id==id)record=edited;};
    require(document.update(update),document.status());
    edited.environment->environment.glow.strength=3;
    require(document.update(update) && document.commit(),document.status());
    require(document.undo() && sameEnvironment(*document.scene().find(id)->environment,*original.environment),"Undo lost the original environment/Glow.");
    require(document.redo() && sameEnvironment(resolveEnvironment(document.scene()),*edited.environment),"Glow did not survive history or inactive IBL.");
    require(document.save("HDR Glow"),document.status());
    const auto saved=readText(project.root/document.assetId());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    require(!reopened.dirty() && sameEnvironment(resolveEnvironment(reopened.scene()),*edited.environment),"Glow save/reopen changed its values or dirty state.");
    document.select({id});require(document.duplicateSelection(),document.status());
    require(sameEnvironment(*document.scene().find(document.selection().primary)->environment,*edited.environment),"Environment duplication lost Glow.");
    for (int version:{1,2,3,4}) {
        auto legacy=Json::parse(saved);legacy["version"]=version;
        for(auto& entity:legacy["entities"])if(entity.contains("environment"))entity["environment"]["environment"].erase("glow");
        if(version==1)legacy["settings"]={{"exposure",1.f},{"environment",{{"map",""},{"enabled",false},{"intensity",1.f},{"rotation",0.f}}}};
        const auto asset="assets/scenes/legacy-glow-"+std::to_string(version)+".crimson-scene";
        const auto contents=legacy.dump();writeText(project.root/asset,contents);
        require(reopened.load(asset),reopened.status());
        require(!resolveEnvironment(reopened.scene()).environment.glow.enabled && readText(project.root/asset)==contents,
            "An older scene enabled Glow or was rewritten on load.");
    }
    require(reopened.load(document.assetId()),reopened.status());
    const auto before=reopened.scene().records();
    for(const float invalid:{-1.f,11.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto invalidRecord=edited;invalidRecord.environment->environment.glow.strength=invalid;
        require(!reopened.updateEntity(id,invalidRecord),"Invalid Glow strength entered the scene.");
        sameRecords(before,reopened.scene().records());
    }
    auto invalid=Json::parse(saved);invalid["entities"][0]["environment"]["environment"]["glow"]["radius"]=-1;
    const auto invalidAsset="assets/scenes/invalid-glow.crimson-scene";writeText(project.root/invalidAsset,invalid.dump());
    require(!reopened.load(invalidAsset),"Malformed Glow settings replaced a working scene.");
    sameRecords(before,reopened.scene().records());
}
void environmentModesPersistenceAndValidation() {
    TempProject project;SceneDocument document(project.root);
    const auto fallback=resolveEnvironment(document.scene());
    require(fallback.environment.source==EnvironmentSource::Hdri && !fallback.environment.background.show &&
        !fallback.environment.fog.enabled && !fallback.environment.debanding && !fallback.sun,"Empty-scene fallback changed its appearance.");
    const auto id=document.createEnvironment("Sky"),sun=document.createDirectionalLight("Sun");
    const auto original=*document.scene().find(id);
    require(original.environment->environment.background.show && !original.environment->environment.debanding,
        "New Environment did not show its background or enabled Debanding by default.");
    auto edited=original;auto& environment=edited.environment->environment;
    edited.environment->sun=sun;edited.environment->exposure=1.8f;
    environment.source=EnvironmentSource::ProceduralSky;environment.map="assets/textures/retained.hdr";
    environment.enabled=false;environment.intensity=2.75f;environment.rotation=37;
    environment.procedural={{.1f,.2f,.3f},{.4f,.5f,.6f},{.7f,.6f,.5f},{.3f,.2f,.1f},.8f,.4f,1.2f,17.f,.6f};
    environment.background={false,true,{.02f,.04f,.08f}};
    environment.fog={true,{.5f,.6f,.7f},.04f};
    environment.fog.mode=FogMode::Depth;environment.fog.depthBegin=12;environment.fog.depthEnd=160;
    environment.fog.depthCurve=1.7f;environment.fog.depthOpacity=.65f;
    environment.fog.height=-3.5f;environment.fog.heightDensity=-.08f;environment.fog.lightEnergy=2.25f;
    environment.debanding=true;
    const auto history=document.history().size();
    require(document.begin("Tune environment"),document.status());
    const auto update=[&](auto& records,auto&){for(auto& record:records)if(record.id==id)record=edited;};
    require(document.update(update),document.status());environment.fog.density=.06f;
    require(document.update(update) && document.commit() && document.history().size()==history+1,document.status());
    require(document.undo() && sameEnvironment(*document.scene().find(id)->environment,*original.environment),"Undo lost environment defaults.");
    require(document.redo() && sameEnvironment(resolveEnvironment(document.scene()),*edited.environment),"Redo lost sky, background, fog or sun settings.");
    auto withoutDebanding=edited;withoutDebanding.environment->environment.debanding=false;
    require(document.updateEntity(id,withoutDebanding) && !resolveEnvironment(document.scene()).environment.debanding,
        "Debanding toggle did not update the scene.");
    require(document.undo() && sameEnvironment(resolveEnvironment(document.scene()),*edited.environment),"Undo lost Debanding settings.");
    require(document.redo() && sameEnvironment(resolveEnvironment(document.scene()),*withoutDebanding.environment),"Redo lost Debanding settings.");
    require(document.undo(),document.status());
    auto hdri=edited;hdri.environment->environment.source=EnvironmentSource::Hdri;
    require(document.updateEntity(id,hdri),document.status());
    require(document.scene().find(id)->environment->environment.procedural==environment.procedural &&
        document.scene().find(id)->environment->sun==sun,"Switching source discarded the inactive procedural settings or sun.");
    require(document.undo(),document.status());
    auto exponential=edited;exponential.environment->environment.fog.mode=FogMode::Exponential;
    require(document.updateEntity(id,exponential),document.status());
    require(document.scene().find(id)->environment->environment.fog==exponential.environment->environment.fog &&
        readEnvironmentSettings(environmentSettingsJson(exponential.environment->environment))==exponential.environment->environment,
        "Switching fog mode discarded inactive depth or exponential settings.");
    require(document.undo() && sameEnvironment(resolveEnvironment(document.scene()),*edited.environment),"Undo did not restore the fog mode and settings.");
    require(document.begin("Cancel fog") && document.update([&](auto& records,auto&){
        for(auto& record:records)if(record.id==id) {
            record.environment->environment.debanding=false;
            auto& fog=record.environment->environment.fog;fog.color={1,0,0};fog.mode=FogMode::Exponential;
            fog.height=8;fog.heightDensity=.3f;fog.lightEnergy=5;
        }
    }),document.status());document.cancel();
    require(sameEnvironment(resolveEnvironment(document.scene()),*edited.environment),"Canceled fog edit changed authored settings.");
    require(readEnvironmentSettings(environmentSettingsJson(environment))==environment,"Shared environment JSON changed settings.");
    for(const auto invalidDebanding:{Json(0),Json(1),Json(1.5),Json("true"),Json(nullptr),Json::array(),Json::object()})
        rejects([&]{auto json=environmentSettingsJson(environment);json["debanding"]=invalidDebanding;(void)readEnvironmentSettings(json);});
    for(const auto invalidSource:{Json(2),Json(-1),Json(1.5),Json("ProceduralSky")})
        rejects([&]{auto json=environmentSettingsJson(environment);json["source"]=invalidSource;(void)readEnvironmentSettings(json);});
    for(const auto invalidMode:{Json(2),Json(-1),Json(1.5),Json(true),Json("Depth"),Json(nullptr)})
        rejects([&]{auto json=environmentSettingsJson(environment);json["fog"]["mode"]=invalidMode;(void)readEnvironmentSettings(json);});
    for(const auto* field:{"density","depthBegin","depthEnd","depthCurve","depthOpacity","height","heightDensity","lightEnergy"})
        for(const auto invalidValue:{Json(true),Json("1"),Json(nullptr)})
            rejects([&]{auto json=environmentSettingsJson(environment);json["fog"][field]=invalidValue;(void)readEnvironmentSettings(json);});
    require(document.save("Environment modes"),document.status());
    const auto saved=readText(project.root/document.assetId());
    require(Json::parse(saved).at("version")==12,"Environment writer did not use scene v12.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    require(!reopened.dirty() && sameEnvironment(resolveEnvironment(reopened.scene()),*edited.environment),"Environment settings did not survive reopening.");
    SceneDocument play(project.root);require(play.resetTransientFrom(document),play.status());
    require(play.transient() && sameEnvironment(resolveEnvironment(play.scene()),*edited.environment),"Play clone lost environment settings or sun link.");
    for(int version=1;version<=5;++version) {
        auto legacy=Json::parse(saved);legacy["version"]=version;
        if(version==1) {
            for(const auto& record:legacy["entities"])if(record.contains("environment"))legacy["settings"]=record["environment"];
            for(auto& record:legacy["entities"])record.erase("environment");
        }
        const auto asset="assets/scenes/environment-v"+std::to_string(version)+".crimson-scene";
        const auto contents=legacy.dump();writeText(project.root/asset,contents);
        require(reopened.load(asset),reopened.status());const auto migrated=resolveEnvironment(reopened.scene());
        require(!reopened.dirty() && readText(project.root/asset)==contents,"Migration rewrote a scene or marked it dirty.");
        require(migrated.environment.source==EnvironmentSource::Hdri && !migrated.environment.background.show &&
            !migrated.environment.background.overrideColor && migrated.environment.fog==FogSettings{} && !migrated.environment.debanding && migrated.sun==NoEntity,
            "Old scene gained a sky background, fog or sun link.");
        require(migrated.environment.map==environment.map && migrated.exposure==edited.environment->exposure &&
            migrated.environment.intensity==environment.intensity,"Migration changed existing HDRI settings.");
    }
    auto versionSix=Json::parse(saved);versionSix["version"]=6;
    for(auto& record:versionSix["entities"])if(record.contains("environment")) {
        record["environment"]["environment"].erase("debanding");
        for(const auto* field:{"mode","depthBegin","depthEnd","depthCurve","depthOpacity","height","heightDensity","lightEnergy"})
            record["environment"]["environment"]["fog"].erase(field);
    }
    const auto versionSixId="assets/scenes/environment-v6.crimson-scene";const auto versionSixText=versionSix.dump();
    writeText(project.root/versionSixId,versionSixText);require(reopened.load(versionSixId),reopened.status());
    auto versionSixExpected=*edited.environment;
    versionSixExpected.environment.debanding=false;
    versionSixExpected.environment.fog={environment.fog.enabled,environment.fog.color,environment.fog.density};
    require(!reopened.dirty() && sameEnvironment(resolveEnvironment(reopened.scene()),versionSixExpected) && readText(project.root/versionSixId)==versionSixText,
        "Scene v6 migration changed existing fog or enabled new depth/height/energy behavior.");
    auto versionSeven=Json::parse(saved);versionSeven["version"]=7;
    for(auto& record:versionSeven["entities"])if(record.contains("environment"))record["environment"]["environment"].erase("debanding");
    const auto versionSevenId="assets/scenes/environment-v7.crimson-scene";const auto versionSevenText=versionSeven.dump();
    writeText(project.root/versionSevenId,versionSevenText);require(reopened.load(versionSevenId),reopened.status());
    auto versionSevenExpected=*edited.environment;versionSevenExpected.environment.debanding=false;
    require(!reopened.dirty() && sameEnvironment(resolveEnvironment(reopened.scene()),versionSevenExpected) && readText(project.root/versionSevenId)==versionSevenText,
        "Scene v7 migration changed existing settings or enabled Debanding.");
    auto minimal=Json::parse(saved);
    for(auto& record:minimal["entities"])if(record.contains("environment")) {
        auto& component=record["environment"];component.erase("sun");auto& settings=component["environment"];
        for(const auto* field:{"source","procedural","background","fog","debanding"})settings.erase(field);
    }
    const auto minimalId="assets/scenes/environment-optional.crimson-scene";writeText(project.root/minimalId,minimal.dump());
    require(reopened.load(minimalId),reopened.status());
    require(!resolveEnvironment(reopened.scene()).environment.background.show && !resolveEnvironment(reopened.scene()).environment.debanding && !resolveEnvironment(reopened.scene()).sun,
        "Absent optional environment fields did not keep compatibility defaults.");
    require(reopened.load(document.assetId()),reopened.status());const auto before=reopened.scene().records();
    for(const auto invalidDebanding:{Json(0),Json(1),Json("false"),Json(nullptr)}) {
        auto invalid=Json::parse(saved);
        for(auto& record:invalid["entities"])if(record.contains("environment"))record["environment"]["environment"]["debanding"]=invalidDebanding;
        const auto invalidId="assets/scenes/invalid-debanding.crimson-scene";writeText(project.root/invalidId,invalid.dump());
        require(!reopened.load(invalidId),"Invalid stored Debanding replaced a working scene.");sameRecords(before,reopened.scene().records());
    }
    const auto rejectEdit=[&](const auto& change) {
        auto candidate=edited;change(*candidate.environment);
        require(!reopened.updateEntity(id,candidate),"Invalid environment edit was accepted.");sameRecords(before,reopened.scene().records());
    };
    rejectEdit([](auto& value){value.environment.source=static_cast<EnvironmentSource>(99);});
    rejectEdit([](auto& value){value.environment.fog.mode=static_cast<FogMode>(99);});
    rejectEdit([](auto& value){value.sun=std::numeric_limits<EntityId>::max();});
    rejectEdit([&](auto& value){value.sun=id;});
    for(float invalid:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        rejectEdit([&](auto& value){value.environment.fog.density=invalid;});
        rejectEdit([&](auto& value){value.environment.procedural.sunDiameter=invalid;});
    }
    for(float invalid:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        rejectEdit([&](auto& value){value.environment.procedural.skyCurve=invalid;});
        rejectEdit([&](auto& value){value.environment.procedural.groundCurve=invalid;});
        rejectEdit([&](auto& value){value.environment.procedural.sunHaloCurve=invalid;});
    }
    for(float invalid:{-1.f,1.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        rejectEdit([&](auto& value){value.environment.procedural.skyTop[0]=invalid;});
        rejectEdit([&](auto& value){value.environment.procedural.skyHorizon[1]=invalid;});
        rejectEdit([&](auto& value){value.environment.procedural.groundBottom[2]=invalid;});
        rejectEdit([&](auto& value){value.environment.procedural.groundHorizon[0]=invalid;});
        rejectEdit([&](auto& value){value.environment.background.color[0]=invalid;});
        rejectEdit([&](auto& value){value.environment.fog.color[0]=invalid;});
    }
    rejectEdit([](auto& value){value.environment.procedural.sunHaloExtent=value.environment.procedural.sunDiameter*.5f;});
    rejectEdit([](auto& value){value.environment.procedural.sunHaloExtent=181;});
    rejectEdit([](auto& value){value.environment.procedural.sunHaloExtent=std::numeric_limits<float>::quiet_NaN();});
    for(const auto member:{&FogSettings::depthBegin,&FogSettings::depthEnd,&FogSettings::depthCurve,&FogSettings::depthOpacity,
            &FogSettings::height,&FogSettings::heightDensity,&FogSettings::lightEnergy})
        for(float invalid:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
            rejectEdit([&](auto& value){value.environment.fog.*member=invalid;});
    rejectEdit([](auto& value){value.environment.fog.depthBegin=-1;});
    rejectEdit([](auto& value){value.environment.fog.depthEnd=-1;});
    rejectEdit([](auto& value){value.environment.fog.depthEnd=value.environment.fog.depthBegin;});
    rejectEdit([](auto& value){value.environment.fog.depthEnd=value.environment.fog.depthBegin-1;});
    rejectEdit([](auto& value){value.environment.fog.depthCurve=0;});
    rejectEdit([](auto& value){value.environment.fog.depthCurve=-1;});
    rejectEdit([](auto& value){value.environment.fog.depthOpacity=-.1f;});
    rejectEdit([](auto& value){value.environment.fog.depthOpacity=1.1f;});
    rejectEdit([](auto& value){value.environment.fog.lightEnergy=-1;});
    auto boundary=environment;boundary.fog.density=0;boundary.procedural.sunDiameter=0;boundary.procedural.sunHaloExtent=180;
    boundary.fog.depthBegin=200;boundary.fog.depthEnd=0;boundary.fog.depthOpacity=0;boundary.fog.lightEnergy=0;boundary.fog.heightDensity=.2f;
    require(readEnvironmentSettings(environmentSettingsJson(boundary))==boundary,"Valid zero density/diameter or maximum halo angle was rejected.");
    auto invalid=Json::parse(saved);
    for(auto& record:invalid["entities"])if(record.contains("environment"))record["environment"]["environment"]["fog"]["density"]=-1;
    const auto invalidId="assets/scenes/invalid-environment.crimson-scene";writeText(project.root/invalidId,invalid.dump());
    require(!reopened.load(invalidId),"Invalid stored fog replaced a working scene.");sameRecords(before,reopened.scene().records());
    for(bool wrongType:{false,true}) {
        auto dangling=Json::parse(saved);
        for(auto& record:dangling["entities"])if(record.contains("environment"))record["environment"]["sun"]=wrongType?record["id"]:Json("ffffffffffffffff");
        const auto danglingId="assets/scenes/dangling-sun.crimson-scene";const auto contents=dangling.dump();writeText(project.root/danglingId,contents);
        require(reopened.load(danglingId),reopened.status());
        require(!reopened.dirty() && !resolveEnvironment(reopened.scene()).sun && readText(project.root/danglingId)==contents,
            "An invalid optional sun did not safely clear without rewriting the source.");
    }
}
void environmentSunHistoryAndDuplication() {
    TempProject project;SceneDocument document(project.root);
    const auto group=document.createEmpty("Sky group"),environmentId=document.createEnvironment("Environment"),sun=document.createDirectionalLight("Sun");
    document.select({environmentId,sun});require(document.reparentSelection(group),document.status());
    auto environment=*document.scene().find(environmentId);environment.environment->sun=sun;
    require(document.updateEntity(environmentId,environment),document.status());
    document.select({environmentId});require(document.duplicateSelection(),document.status());
    const auto externalLink=document.selection().primary;
    require(document.scene().find(externalLink)->environment->sun==sun,"Duplicating only Environment lost its external sun reference.");
    document.select({group});require(document.duplicateSelection(),document.status());const auto copiedGroup=document.selection().primary;
    EntityId copiedEnvironment{},copiedSun{};
    for(auto child:document.scene().children(copiedGroup)) {
        const auto record=document.scene().find(child);
        if(record->directional)copiedSun=child;
        if(record->environment && child!=externalLink)copiedEnvironment=child;
    }
    require(copiedEnvironment && copiedSun && copiedSun!=sun && document.scene().find(copiedEnvironment)->environment->sun==copiedSun,
        "Subtree duplication did not remap an internal sun link.");
    require(document.scene().find(environmentId)->environment->sun==sun,"Subtree duplication rewrote the original sun link.");
    document.select({sun});const auto history=document.history().size();require(document.eraseSelection(),document.status());
    require(document.history().size()==history+1 && document.scene().find(environmentId)->environment->sun==NoEntity &&
        document.scene().find(externalLink)->environment->sun==NoEntity,"Deleting a sun did not clear all links in one transaction.");
    require(document.scene().find(copiedEnvironment)->environment->sun==copiedSun,"Deleting the original sun altered the copy's sun.");
    require(document.undo() && document.scene().find(environmentId)->environment->sun==sun &&
        document.scene().find(externalLink)->environment->sun==sun,"Undo did not restore sun identity and dependent links.");
    require(document.redo() && !document.scene().find(environmentId)->environment->sun,"Redo did not clear the sun link again.");
}
void environmentAmbientLightStorageAndHistory() {
    TempProject project;SceneDocument document(project.root);
    require(resolveEnvironment(document.scene()).environment.ambientLight==AmbientLightSettings{},
        "Empty scenes changed their existing sky ambient defaults.");
    const auto id=document.createEnvironment("Ambient");
    const auto original=*document.scene().find(id);
    auto edited=original;
    edited.environment->environment.ambientLight={AmbientLightSource::Sky,{.23f,.41f,.67f},.35f,2.75f};
    require(document.updateEntity(id,edited),document.status());
    require(document.undo() && sameEnvironment(resolveEnvironment(document.scene()),*original.environment),
        "Ambient light undo lost original sky lighting.");
    require(document.redo() && sameEnvironment(resolveEnvironment(document.scene()),*edited.environment),
        "Ambient light redo lost its source, color, sky contribution or energy.");
    for(const auto source:{AmbientLightSource::Disabled,AmbientLightSource::Color,AmbientLightSource::Sky}) {
        auto changed=edited;changed.environment->environment.ambientLight.source=source;
        require(document.updateEntity(id,changed),document.status());
        require(document.save("Ambient"),document.status());
        SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
        require(!reopened.dirty() && sameEnvironment(resolveEnvironment(reopened.scene()),*changed.environment),
            "Ambient mode round-trip discarded its inactive authored values.");
        require(readEnvironmentSettings(environmentSettingsJson(changed.environment->environment))==changed.environment->environment,
            "Shared scene and legacy environment persistence disagree for ambient light.");
    }
    document.select({id});require(document.duplicateSelection(),document.status());
    require(sameEnvironment(*document.scene().find(document.selection().primary)->environment,*edited.environment),
        "Environment duplication discarded ambient light settings.");
    require(document.save("Ambient"),document.status());
    const auto saved=Json::parse(readText(project.root/document.assetId()));
    auto legacy=saved;
    for(auto& record:legacy["entities"])if(record.contains("environment"))record["environment"]["environment"].erase("ambientLight");
    const auto legacyText=legacy.dump();const auto legacyId="assets/scenes/ambient-missing.crimson-scene";
    writeText(project.root/legacyId,legacyText);
    SceneDocument reopened(project.root);require(reopened.load(legacyId),reopened.status());
    require(!reopened.dirty() && readText(project.root/legacyId)==legacyText &&
        resolveEnvironment(reopened.scene()).environment.ambientLight==AmbientLightSettings{},
        "Old scenes must retain sky diffuse lighting without rewriting their source.");
    auto settings=edited.environment->environment;
    const auto reject=[&](const auto& change){auto candidate=settings;change(candidate.ambientLight);rejects([&]{validateEnvironmentSettings(candidate);});};
    reject([](auto& ambient){ambient.source=static_cast<AmbientLightSource>(99);});
    for(float invalid:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        reject([&](auto& ambient){ambient.energy=invalid;});
    for(float invalid:{-1.f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        reject([&](auto& ambient){ambient.skyContribution=invalid;});
        reject([&](auto& ambient){ambient.color[1]=invalid;});
    }
    for(const auto invalidSource:{Json(-1),Json(3),Json(1.5),Json(true),Json("Sky"),Json(nullptr)})
        rejects([&]{auto json=environmentSettingsJson(settings);json["ambientLight"]["source"]=invalidSource;(void)readEnvironmentSettings(json);});
    for(const auto* field:{"skyContribution","energy"})for(const auto invalidValue:{Json(true),Json("1"),Json(nullptr)})
        rejects([&]{auto json=environmentSettingsJson(settings);json["ambientLight"][field]=invalidValue;(void)readEnvironmentSettings(json);});
    for(const auto invalidObject:{Json(true),Json(1),Json(nullptr),Json::array()})
        rejects([&]{auto json=environmentSettingsJson(settings);json["ambientLight"]=invalidObject;(void)readEnvironmentSettings(json);});
    settings.ambientLight.energy=0;settings.ambientLight.skyContribution=0;settings.ambientLight.color={0,1,0};
    require(readEnvironmentSettings(environmentSettingsJson(settings))==settings,"Valid ambient light boundaries were rejected.");
    require(document.save("Ambient"),document.status());
    require(reopened.load(document.assetId()),reopened.status());
    const auto before=reopened.scene().records();
    auto invalid=saved;
    for(auto& record:invalid["entities"])if(record.contains("environment"))record["environment"]["environment"]["ambientLight"]["energy"]=-1;
    const auto invalidId="assets/scenes/ambient-invalid.crimson-scene";writeText(project.root/invalidId,invalid.dump());
    require(!reopened.load(invalidId),"Invalid ambient light replaced a working scene.");sameRecords(before,reopened.scene().records());
}
void particleReferencesAndPersistence() {
    TempProject project;SceneDocument document(project.root);
    const std::string effect="assets/particles/missing.crimson-particle";
    Mat4 local=math::translation({3,2,-1})*math::scaling({-2,3,4});local[4]=.7f;
    const auto documentGeneration=document.documentGeneration();
    const auto particle=document.createParticle(effect,"Sparks",local);
    require(particle && document.scene().find(particle)->particle->id==effect,"Particle placement lost its authored unresolved reference.");
    require(document.documentGeneration()==documentGeneration,"Particle placement replaced the document lifetime.");
    const auto group=document.createEmpty("Parent");auto parent=*document.scene().find(group);
    parent.local=math::translation({-4,1,5})*math::scaling({2,3,.5f});
    require(document.updateEntity(group,parent),document.status());document.select({particle});
    require(document.reparentSelection(group),document.status());nearMatrix(*document.scene().worldTransform(particle),local);
    const auto child=document.createEmpty("Attached child");document.select({child});
    require(document.reparentSelection(particle),document.status());document.select({particle});
    require(!document.ungroupSelection(),"A particle emitter with children was treated as an empty group.");
    require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(duplicate!=particle && document.scene().find(duplicate)->particle==document.scene().find(particle)->particle,"Particle duplication did not retain the shared asset reference.");
    require(document.eraseSelection(),document.status());require(document.undo(),document.status());
    require(document.scene().find(duplicate)->particle->id==effect,"Undo did not restore particle identity and reference.");
    const auto historyReferences=document.historicalParticleReferences();
    require(historyReferences.size()==1 && historyReferences.front()==effect,"History omitted or duplicated particle asset references.");
    const auto before=document.scene().records();const auto history=document.history().size();
    auto invalid=*document.scene().find(particle);invalid.particle=AssetRef<ParticleEffectAsset>{"assets/textures/wrong.png"};
    require(!document.updateEntity(particle,invalid),"A foreign typed particle asset path was accepted.");
    sameRecords(before,document.scene().records());require(document.history().size()==history,"Rejected particle reference modified history.");
    invalid.particle=AssetRef<ParticleEffectAsset>{"assets/particles/../bad.crimson-particle"};
    require(!document.updateEntity(particle,invalid),"Particle traversal was accepted.");
    require(document.save("Particles"),document.status());const auto savedId=document.assetId();
    const auto serialized=Json::parse(readText(project.root/savedId));require(serialized.at("version")==12,"Particle scene writer did not use version 12.");
    SceneDocument reopened(project.root);require(reopened.load(savedId),reopened.status());sameRecords(document.scene().records(),reopened.scene().records());
    require(!reopened.dirty() && reopened.scene().find(particle)->particle->id==effect,"Missing particle asset prevented clean scene reopening.");
    auto cleared=*reopened.scene().find(particle);cleared.particle=AssetRef<ParticleEffectAsset>{};
    require(reopened.updateEntity(particle,cleared) && reopened.save(),reopened.status());
    SceneDocument emptyReference(project.root);require(emptyReference.load(savedId),emptyReference.status());
    require(emptyReference.scene().find(particle)->particle && emptyReference.scene().find(particle)->particle->empty(),"An explicitly cleared emitter lost its marker component.");
    auto versionTwo=serialized;versionTwo["version"]=2;for(auto& entity:versionTwo["entities"])entity.erase("particle");
    const std::string legacy="assets/scenes/version-two.crimson-scene";writeText(project.root/legacy,versionTwo.dump());
    const auto oldBytes=readText(project.root/legacy);SceneDocument old(project.root);require(old.load(legacy),old.status());
    require(!old.dirty() && readText(project.root/legacy)==oldBytes,"Version-two load rewrote its source or became dirty.");
    for(const auto& record:old.scene().records())require(!record.particle,"Version-two migration invented a particle component.");
    require(old.save(),old.status());require(Json::parse(readText(project.root/legacy)).at("version")==12,"Explicit version-two save did not migrate to version twelve.");
}
void externalSceneChanges() {
    TempProject project; SceneDocument document(project.root);
    const auto model=document.createModel("assets/models/one/manifest.json","Model");
    Camera preview; preview.position={4,5,6}; document.setPreviewCamera(preview);
    require(document.save("Original"),document.status()); const auto id=document.assetId();
    const auto baseline=document.loadedFingerprint(); auto snapshot=readSceneSnapshot(project.root,id);
    require(snapshot.previewCamera && snapshot.previewCamera->position==preview.position,"Saved scene preview camera was lost.");
    snapshot.name="External"; snapshot.records.front().name="Changed externally"; saveSceneSnapshot(project.root,id,snapshot);
    require(document.externalFileChanged() && document.loadedFingerprint()==baseline,"External scene changes were not detected.");
    const auto history=document.history().size(); const auto before=document.scene().records();
    require(!document.save() && document.history().size()==history,"Save silently overwrote external content."); sameRecords(document.scene().records(),before);
    document.select({model}); require(document.reloadFromDisk(),document.status());
    require(document.displayName()=="External" && document.selection().primary==model && !document.dirty(),"Reload did not preserve surviving selection or clear history.");
    document.select({model}); require(document.eraseSelection(),document.status());
    require(document.historicalModelReferences()==std::vector<std::string>{"assets/models/one/manifest.json"},"History dependency tracking missed a deleted model.");
    require(document.applyExternalAssetChanges(document.scene().records(),document.scene().properties(),"Resources updated",true),document.status());
    require(document.dirty() && !document.history().canUndo(),"Global resource edit restored invalid undo history or cleared unsaved state.");
    auto invalid=readSceneSnapshot(project.root,id); invalid.properties.activeEnvironment=999;
    const auto current=document.scene().records(); require(!document.applyReloadSnapshot(invalid),"Malformed reload applied."); sameRecords(document.scene().records(),current);
    require(document.save({},false,true) && !document.externalFileChanged(),document.status());
}
void preparedSceneOpening() {
    TempProject project; SceneDocument source(project.root);
    const auto sourceId=source.createEmpty("Prepared entity"); Camera preview; preview.position={3,4,5}; source.setPreviewCamera(preview);
    require(source.save("Worker-loaded scene"),source.status());
    const auto prepared=readSceneSnapshot(project.root,source.assetId());
    SceneDocument document(project.root); const auto oldId=document.createEmpty("Keep on failure"); document.select({oldId});
    auto malformed=prepared; malformed.records.front().local[0]=std::numeric_limits<float>::quiet_NaN();
    const auto records=document.scene().records();
    const auto generation=document.documentGeneration();
    const auto history=document.history().size();
    require(!document.openPreparedSnapshot(malformed),"An invalid worker candidate replaced the active scene.");
    sameRecords(document.scene().records(),records);
    require(document.documentGeneration()==generation && document.history().size()==history && document.selection().primary==oldId && document.dirty(),
        "Failed prepared open changed document generation, history, selection or dirty state.");
    // Publication consumes the already decoded CPU document; it must not read
    // the managed scene file again on the editor thread.
    std::filesystem::remove(project.root/prepared.id);
    require(document.openPreparedSnapshot(prepared),document.status());
    require(document.scene().contains(sourceId) && document.assetId()==prepared.id && document.displayName()==prepared.name &&
        document.documentGeneration()==generation+1 && !document.dirty() && document.history().size()==0 && document.selection().ids.empty() &&
        document.loadedFingerprint()==prepared.fingerprint && document.previewCamera() && document.previewCamera()->position==preview.position,
        "Prepared open lost identity, preview camera or new-document history semantics.");
}
void preparedGlobalChanges() {
    TempProject project;SceneDocument document(project.root);const auto removed=document.createModel("assets/models/delete/manifest.json","Remove");
    const auto kept=document.createEmpty("Keep");document.select({removed,kept},removed);Camera camera;camera.position={7,8,9};document.setPreviewCamera(camera);
    require(document.save("Prepared"),document.status());const auto id=document.assetId();const auto before=document.scene().records();const auto history=document.history().size();
    auto invalid=before;invalid.front().local[15]=0;bool rejected=false;
    try{(void)document.prepareExternalAssetChanges(invalid,document.scene().properties(),"invalid",false,document.loadedFingerprint());}catch(const std::exception&){rejected=true;}
    require(rejected&&!document.dirty()&&document.history().size()==history,"Failed preparation changed document/history.");sameRecords(document.scene().records(),before);
    auto candidate=before;std::erase_if(candidate,[&](const auto& record){return record.id==removed;});
    auto saved=readSceneSnapshot(project.root,id);saved.records=candidate;const auto text=serializeSceneSnapshot(saved);
    {
        auto canceled=document.prepareExternalAssetChanges(candidate,saved.properties,"Deleted resources",false,sceneTextFingerprint(text));
        require(document.canCommitExternalAssetChanges(canceled),"Fresh prepared registry was stale.");
        bool failed=false;try{writeFileAtomically(project.root/"assets/scenes",text);}catch(const std::exception&){failed=true;}
        require(failed&&document.history().size()==history&&!document.dirty(),"Failed file publication changed the active document.");sameRecords(document.scene().records(),before);
    }
    auto clean=document.prepareExternalAssetChanges(candidate,saved.properties,"Deleted resources",false,sceneTextFingerprint(text));
    writeFileAtomically(project.root/id,text);require(document.commitExternalAssetChanges(std::move(clean)),"Prepared registry commit failed after durable publication.");
    require(!document.dirty()&&!document.history().canUndo()&&document.selection().primary==kept&&!document.externalFileChanged(),"Clean global deletion lost selection, fingerprint or saved state.");
    require(!document.commitExternalAssetChanges(std::move(clean)),"A consumed prepared scene was committed twice.");
    const auto added=document.createModel("assets/models/temporary/manifest.json","Temporary");auto edited=*document.scene().find(kept);edited.name="Unsaved name";require(document.updateEntity(kept,edited),document.status());
    auto dirty=document.scene().records();std::erase_if(dirty,[&](const auto& record){return record.id==added;});
    auto pending=document.prepareExternalAssetChanges(dirty,document.scene().properties(),"Deleted resources",true,document.loadedFingerprint());
    require(document.commitExternalAssetChanges(std::move(pending))&&document.dirty()&&document.history().size()==0,"Dirty global change saved unrelated edits or retained invalid history.");
    require(readSceneSnapshot(project.root,id).records.front().name=="Keep"&&document.scene().find(kept)->name=="Unsaved name","Global deletion silently saved unrelated active-document edits.");
    auto stale=document.prepareExternalAssetChanges(document.scene().records(),document.scene().properties(),"Stale",true,document.loadedFingerprint());
    document.createEmpty("Another edit");require(!document.canCommitExternalAssetChanges(stale)&&!document.commitExternalAssetChanges(std::move(stale)),"Prepared transaction overwrote a newer scene edit.");
    auto newScene=document.prepareNewSceneAfterAssetDeletion();const auto generation=document.documentGeneration();std::filesystem::remove(project.root/id);
    require(document.commitExternalAssetChanges(std::move(newScene))&&document.assetId().empty()&&document.scene().records().empty()&&document.dirty()&&
        document.documentGeneration()==generation+1&&!document.previewCamera(),"Deleting active scene did not publish a prepared empty document.");
}

void inspectorAffineTransactions() {
    TempProject project;SceneDocument document(project.root);
    const auto parent=document.createEmpty("Mirrored affine parent");
    const auto model=document.createModel("assets/models/inspector/manifest.json","Inspector model");
    const auto light=document.createDirectionalLight("Child light");
    const auto sibling=document.createEmpty("Unrelated sibling");
    Mat4 parentShear;parentShear[4]=.32f;parentShear[8]=-.18f;parentShear[9]=.21f;
    Mat4 localShear;localShear[4]=-.23f;localShear[8]=.17f;localShear[9]=.29f;
    const auto parentLocal=math::translation({3,-2,4})*math::rotation(math::axisAngle({0,1,0},.57))*parentShear*math::scaling({-2,3,.65f});
    const auto modelLocal=math::translation({1.5f,2,-3})*math::rotation(math::axisAngle({1,2,3},-.43))*localShear*math::scaling({1.4f,-.75f,2.1f});
    require(document.edit("Inspector fixture",[&](auto& records,auto&){for(auto& entity:records){
        if(entity.id==parent){entity.local=parentLocal;entity.order=0;}
        else if(entity.id==model){entity.parent=parent;entity.order=0;entity.local=modelLocal;}
        else if(entity.id==light){entity.parent=model;entity.order=0;entity.local=math::translation({.5f,1,-.25f});}
        else if(entity.id==sibling)entity.order=1;
    }}),document.status());
    require(document.save("Inspector affine gestures"),document.status());const auto sceneId=document.assetId();

    for(unsigned operation=0;operation<3;++operation){
        require(document.load(sceneId),document.status());document.select({model},model);
        const auto baseline=document.scene().records();const auto captured=*document.scene().find(model);
        const auto fields=math::affineTransformFields(captured.local);
        require(fields&&fields->sheared&&!fields->singular,"Inspector fixture did not retain editable sheared affine fields.");
        const auto generation=document.documentGeneration();
        const auto original=operation==0?fields->position:operation==1?fields->rotationRadians:fields->scale;
        auto first=original,second=original;
        if(operation==0){first=original+Vec3{.25f,-.5f,1};second=original+Vec3{2,-1.25f,.5f};}
        else if(operation==1){first=original+Vec3{.1f,-.2f,.05f};second=original+Vec3{-.25f,.35f,.3f};}
        else {first[0]*=1.25f;first[2]*=.7f;second[0]*=-.75f;second[2]*=1.4f;}
        const auto edited=[&](const Vec3& requested){
            return operation==0?math::withAffinePosition(captured.local,requested):
                operation==1?math::withAffineRotation(captured.local,*fields,requested):math::withAffineScale(captured.local,*fields,requested);
        };
        const auto update=[&](const Vec3& requested){return document.update([&](auto& records,auto&){
            const auto result=edited(requested);if(!result)throw std::runtime_error("Inspector helper rejected a valid affine edit.");
            for(auto& entity:records)if(entity.id==model)entity.local=*result;
        });};
        const auto expected=edited(second);require(expected.has_value(),"Inspector helper rejected requested transform fields.");
        const auto label=operation==0?"Edit position":operation==1?"Edit rotation":"Edit scale";
        require(document.begin(label)&&update(first)&&update(second),document.status());
        require(document.dirty()&&document.history().size()==0,"Continuous Inspector updates entered history before commit or stayed clean.");
        require(document.scene().find(model)->local==*expected,"Inspector gesture accumulated prior updates instead of using its captured affine matrix.");
        require(document.scene().find(parent)->local==parentLocal&&document.scene().find(light)->parent==model&&document.scene().find(sibling)->parent==NoEntity,
            "Inspector local edit changed the parent or unrelated hierarchy.");
        nearMatrix(*document.scene().worldTransform(model),parentLocal**expected);
        nearMatrix(*document.scene().worldTransform(light),parentLocal**expected*document.scene().find(light)->local);
        require(document.commit()&&document.history().size()==1&&document.history().undoLabel()==label,"One Inspector gesture did not produce exactly one labeled command.");
        const auto committed=document.scene().records();
        require(document.undo()&&!document.dirty(),"Undo Inspector gesture did not return to the saved state.");sameRecords(document.scene().records(),baseline);
        require(document.redo()&&document.dirty(),"Redo Inspector gesture did not restore its dirty state.");sameRecords(document.scene().records(),committed);
        require(document.selection().ids==std::vector<EntityId>{model}&&document.scene().find(model)->model->id=="assets/models/inspector/manifest.json"&&
            document.scene().contains(parent)&&document.scene().contains(light)&&document.scene().contains(sibling)&&document.documentGeneration()==generation,
            "Inspector undo/redo changed selection, persistent IDs, asset reference or document identity.");
        require(document.undo()&&!document.dirty(),"Cannot restore clean state before canceled Inspector edit.");
        require(document.begin("Cancel Inspector edit")&&update(second),document.status());
        document.select({parent},parent);require(document.selection().primary==model,"Selection changed inside an Inspector transaction.");
        document.cancel();sameRecords(document.scene().records(),baseline);
        require(!document.dirty()&&document.history().size()==1&&document.history().canRedo()&&document.selection().primary==model,
            "Canceled Inspector edit changed dirty/history/selection or failed to preserve redo.");
        require(edited(original)&&*edited(original)==captured.local,"An unchanged Inspector field did not return the exact captured affine matrix.");
        require(document.begin("Unchanged Inspector field")&&update(original)&&document.commit(),document.status());
        sameRecords(document.scene().records(),baseline);
        require(!document.dirty()&&document.history().size()==1&&document.history().canRedo(),"An unchanged Inspector edit created history or cleared redo.");
        const auto unsaved=document.createEmpty("Existing unsaved edit");require(unsaved&&document.dirty(),"Cannot create a dirty cancellation fixture.");
        document.select({model},model);const auto dirtyRecords=document.scene().records();const auto dirtyHistory=document.history().size();
        require(document.begin("Cancel with existing edits")&&update(first),document.status());document.cancel();sameRecords(document.scene().records(),dirtyRecords);
        require(document.dirty()&&document.history().size()==dirtyHistory&&document.scene().contains(unsaved),"Canceled Inspector edit cleared unrelated unsaved changes.");
    }
}

void inspectorZeroScaleHistory() {
    TempProject project;SceneDocument document(project.root);
    const auto parent=document.createEmpty("Affine parent");
    const auto model=document.createModel("assets/models/zero-scale/manifest.json","Zero-scale model");
    const auto light=document.createPointLight("Surviving child");
    Mat4 shear;shear[4]=.27f;shear[8]=-.19f;shear[9]=.14f;
    require(document.edit("Zero-scale hierarchy",[&](auto& records,auto&){for(auto& entity:records){
        if(entity.id==parent){entity.order=0;entity.local=math::translation({2,3,-4})*math::rotation(math::axisAngle({1,2,0},.38))*shear*math::scaling({-2,.75f,3});}
        else if(entity.id==model){entity.parent=parent;entity.order=0;entity.local=math::translation({1,-2,3})*math::rotation(math::axisAngle({0,1,2},-.51))*shear*math::scaling({1.25f,2,-.8f});}
        else if(entity.id==light){entity.parent=model;entity.order=0;entity.local=math::translation({.5f,1,2});}
    }}),document.status());
    require(document.save("Nonzero baseline"),document.status());const auto baselineId=document.assetId();
    const auto updateScale=[&](EntityId id,const Mat4& captured,const math::AffineTransformFields& fields,const Vec3& requested){
        return document.update([&](auto& records,auto&){
            const auto result=math::withAffineScale(captured,fields,requested);
            if(!result)throw std::runtime_error("Inspector rejected a finite zero/recovered scale.");
            for(auto& entity:records)if(entity.id==id)entity.local=*result;
        });
    };
    for(const auto target:std::array<EntityId,2>{parent,model})for(const unsigned mask:std::array<unsigned,4>{1,2,4,7}){
        require(document.load(baselineId),document.status());document.select({target},target);
        const auto before=document.scene().records();const auto captured=document.scene().find(target)->local;
        const auto fields=math::affineTransformFields(captured);require(fields&&!fields->singular,"Zero-scale baseline is already singular.");
        auto approaching=fields->scale,zero=fields->scale;
        for(size_t axis=0;axis<3;++axis)if(mask&(1u<<axis)){approaching[axis]*=.5f;zero[axis]=0;}
        const auto zeroMatrix=math::withAffineScale(captured,*fields,zero);require(zeroMatrix&&!math::inverse(*zeroMatrix),"Zero scale did not produce an accepted singular local matrix.");
        require(document.begin("Set scale to zero")&&updateScale(target,captured,*fields,approaching)&&updateScale(target,captured,*fields,zero),document.status());
        require(document.transactionActive()&&document.dirty()&&document.history().size()==0,"Zero-scale updates escaped their single active transaction.");
        require(document.commit()&&document.history().size()==1&&document.history().undoLabel()=="Set scale to zero","Zero scale was not committed as one undoable command.");
        const auto collapsed=document.scene().records();
        require(document.scene().find(target)->local==*zeroMatrix&&document.scene().find(model)->parent==parent&&document.scene().find(light)->parent==model&&
            math::finite(*document.scene().worldTransform(model))&&math::finite(*document.scene().worldTransform(light)),
            "Zero scale changed hierarchy/IDs or prevented finite CPU world transforms.");
        require(document.undo()&&!document.dirty(),"Undo zero scale failed to restore the saved state.");sameRecords(document.scene().records(),before);
        require(document.redo()&&document.dirty(),"Redo zero scale failed.");sameRecords(document.scene().records(),collapsed);
        require(document.selection().ids==std::vector<EntityId>{target}&&document.scene().find(model)->model->id=="assets/models/zero-scale/manifest.json",
            "Zero-scale history changed selection or the model asset identity.");
        require(document.save(availableSceneName(project.root,"Saved zero scale"),true),document.status());const auto zeroId=document.assetId();
        require(!document.dirty()&&document.history().size()==1,"Saving zero scale changed committed history or failed to mark clean.");
        require(document.load(zeroId),document.status());document.select({target},target);
        const auto reopened=document.scene().records();sameRecords(reopened,collapsed);
        require(!document.dirty()&&document.history().size()==0&&!document.undo(),"Reopening zero scale did not establish a clean scene-scoped history.");
        const auto reopenedLocal=document.scene().find(target)->local;const auto reopenedFields=math::affineTransformFields(reopenedLocal);
        require(reopenedFields&&reopenedFields->singular,"Reopened zero scale lost its editable singular fields.");
        require(document.begin("Unchanged zero scale")&&updateScale(target,reopenedLocal,*reopenedFields,reopenedFields->scale)&&document.commit(),document.status());
        sameRecords(document.scene().records(),reopened);require(!document.dirty()&&document.history().size()==0,"No-op zero scale created history or marked the scene dirty.");
        auto recovered=reopenedFields->scale,partial=recovered;
        const Vec3 requested{1.75f,-1.25f,.9f};
        for(size_t axis=0;axis<3;++axis)if(mask&(1u<<axis)){recovered[axis]=requested[axis];partial[axis]=requested[axis]*.5f;}
        const auto recoveredMatrix=math::withAffineScale(reopenedLocal,*reopenedFields,recovered);
        require(recoveredMatrix&&math::inverse(*recoveredMatrix),"Reopened zero axes could not be recovered to an invertible local matrix.");
        require(document.begin("Cancel zero-scale recovery")&&updateScale(target,reopenedLocal,*reopenedFields,recovered),document.status());document.cancel();
        sameRecords(document.scene().records(),reopened);require(!document.dirty()&&document.history().size()==0,"Canceled recovery failed to restore exact saved zero scale.");
        require(document.begin("Recover zero scale")&&updateScale(target,reopenedLocal,*reopenedFields,partial)&&updateScale(target,reopenedLocal,*reopenedFields,recovered)&&document.commit(),document.status());
        require(document.history().size()==1&&document.dirty()&&document.scene().find(target)->local==*recoveredMatrix,"Recovery was not one exact captured-input command.");
        const auto restored=document.scene().records();
        require(document.undo()&&!document.dirty(),"Undo recovery did not restore the clean reopened zero matrix.");sameRecords(document.scene().records(),reopened);
        require(document.redo()&&document.dirty(),"Redo recovery failed to restore nonzero scale.");sameRecords(document.scene().records(),restored);
        const auto restoredFields=math::affineTransformFields(*recoveredMatrix);require(restoredFields.has_value(),"Recovered matrix lost Inspector fields.");
        require(document.begin("Cancel with recovered unsaved edits")&&updateScale(target,*recoveredMatrix,*restoredFields,Vec3{0,0,0}),document.status());document.cancel();
        sameRecords(document.scene().records(),restored);require(document.dirty()&&document.history().size()==1&&document.selection().primary==target,
            "Cancellation discarded existing recovered-scale edits, changed history or selection.");
    }
}
}

int main() {
    try {
        registryAndHierarchy();physicalSceneStorage();
        reparentPreservesAffine();
        transactionsAndSelection();
        viewportParentSelection();
        subtreeCommands();
        persistenceAndCatalog();
        dirtyTokensAndFailure();
        limitsAndHistoryBound();
        groupingAndBatchPlacement();
        environmentsAndMigration();environmentGlowHistoryAndMigration();environmentModesPersistenceAndValidation();environmentSunHistoryAndDuplication();environmentAmbientLightStorageAndHistory();
        particleReferencesAndPersistence();externalSceneChanges();preparedSceneOpening();preparedGlobalChanges();inspectorAffineTransactions();inspectorZeroScaleHistory();
        std::cout << "Scene core: 21 groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene core failed: " << error.what() << '\n';
        return 1;
    }
}
