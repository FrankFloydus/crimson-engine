#include "scene/SceneDocument.h"
#include "math/Json.h"

#include <nlohmann/json.hpp>
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

void registryAndHierarchy() {
    Scene scene;
    EntityRecord root; root.id=1; root.name="Root";
    root.local=math::translation({3,2,-5}) * math::rotation(math::axisAngle({0,1,0},0.5)) * math::scaling({-2,3,0.5});
    EntityRecord child; child.id=2; child.name="Child"; child.parent=1;
    child.local=math::translation({1,2,3}); child.local[4]=0.3f;
    child.model="assets/models/missing/manifest.json";
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
    malformed=records; malformed[1].model="../outside.glb";
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
    auto settings=document.scene().properties(); settings.environment.map="assets/textures/missing.hdr";
    settings.environment.rotation=35; settings.environment.intensity=2; settings.exposure=1.7f;
    require(document.setProperties(settings),document.status());
    const auto records=document.scene().records();
    require(document.save("Shared name"),document.status());
    const auto firstId=document.assetId();
    require(!document.dirty() && !firstId.empty(),"Save did not mark document clean.");
    require(document.save("Shared name",true),document.status());
    const auto secondId=document.assetId();
    require(firstId!=secondId,"Save As reused the existing asset ID.");
    auto catalog=scanSceneAssets(project.root);
    require(catalog.size()==2 && catalog[0].name=="Shared name" && catalog[1].name=="Shared name","Catalog lost colliding scene names.");
    SceneDocument reopened(project.root);
    require(reopened.load(firstId),reopened.status());
    sameRecords(reopened.scene().records(),records);
    require(sameProperties(reopened.scene().properties(),settings),"Scene settings did not round-trip.");
    require(reopened.scene().find(id)->model==std::optional<std::string>("assets/models/missing/manifest.json"),"Missing model reference was erased.");
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
    for (size_t i=0;i<MaximumPointLights;++i) { point=document.createPointLight(); require(point!=NoEntity,document.status()); }
    const auto records=document.scene().records(); document.select({point});
    require(!document.duplicateSelection(),"Duplicating beyond light limit succeeded.");
    sameRecords(document.scene().records(),records);
    auto invalid=*document.scene().find(point); invalid.point->intensity=-1;
    require(!document.updateEntity(point,invalid),"Negative intensity accepted.");
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
}

int main() {
    try {
        registryAndHierarchy();
        reparentPreservesAffine();
        transactionsAndSelection();
        subtreeCommands();
        persistenceAndCatalog();
        dirtyTokensAndFailure();
        limitsAndHistoryBound();
        groupingAndBatchPlacement();
        std::cout << "Scene core: 8 groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene core failed: " << error.what() << '\n';
        return 1;
    }
}
