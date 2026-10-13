#include "assets/BrowserCatalog.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace crimson;
namespace fs = std::filesystem;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void rejects(Action action,const char* message) {
    bool failed=false;
    try { action(); } catch (const std::exception&) { failed=true; }
    require(failed,message);
}
struct TemporaryProject {
    fs::path root=fs::temp_directory_path()/("crimson-browser-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryProject() { fs::create_directories(root/"assets"); }
    ~TemporaryProject() { std::error_code ignored; fs::remove_all(root,ignored); }
};
std::string read(const fs::path& file) {
    std::ifstream input(file,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void write(const fs::path& file,const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream output(file,std::ios::binary); output<<text;
    require(static_cast<bool>(output),"Cannot write the isolated test fixture.");
}
const std::array<AssetEntry,4> assets{{
    {"assets/textures/albedo.png","Albedo.png",AssetKind::Texture},
    {"assets/models/tree/manifest.json","Tree",AssetKind::Model},
    {"assets/scenes/level.json","Level",AssetKind::Scene},
    {"assets/textures/studio.hdr","Studio.hdr",AssetKind::Environment}
}};

void metadataAndIdentity() {
    TemporaryProject project;
    for (const auto& asset:assets) write(project.root/asset.id,"managed content: "+asset.id);
    BrowserCatalog catalog(project.root);
    catalog.synchronize(assets);
    require(!fs::exists(project.root/"assets/browser.json"),"Opening the browser wrote project metadata without an edit.");
    require(catalog.items().size()==4 && catalog.items()[0].id==assets[0].id,"Initial catalog order is not alphabetical.");
    const auto art=catalog.createFolder({},"  Art  ");
    const auto models=catalog.createFolder(art,"Models");
    require(catalog.name(art)=="Art" && catalog.items()[0].id==art,"Folder name trimming or folder-first sorting failed.");
    rejects([&]{catalog.createFolder({},"aRT");},"Case-insensitive sibling folder collision was accepted.");
    rejects([&]{catalog.createFolder({},"../Outside");},"A path was accepted as a browser folder name.");
    rejects([&]{catalog.rename(art,"..");},"A traversal name was accepted during rename.");
    catalog.move(std::array{assets[1].id},models);
    catalog.move(std::array{assets[0].id,assets[3].id},art);
    catalog.rename(assets[1].id,"Ancient Tree");
    catalog.rename(art,"Environment Art");
    require(catalog.items(models).size()==1 && catalog.items(models)[0].id==assets[1].id && catalog.items(models)[0].name=="Ancient Tree",
            "Renaming/moving changed the stable asset identity or display name.");
    require(catalog.parent(assets[0].id)==art && catalog.parent(assets[3].id)==art,"Mixed assets did not move together.");
    const auto breadcrumbs=catalog.breadcrumbs(models);
    require(breadcrumbs.size()==2 && breadcrumbs[0].id==art && breadcrumbs[0].name=="Environment Art" && breadcrumbs[1].id==models,
            "Nested folder breadcrumbs did not follow a renamed parent.");
    for (const auto& asset:assets)
        require(read(project.root/asset.id)=="managed content: "+asset.id,"Browser organization modified a managed source/package file.");

    BrowserCatalog reopened(project.root);
    reopened.synchronize(assets);
    require(reopened.status().empty() && reopened.name(assets[1].id)=="Ancient Tree" && reopened.parent(assets[1].id)==models,
            "Browser names/folders were not restored by stable ID.");
    reopened.synchronize({});
    reopened.rename(art,"Art"); // Saving while a managed asset is temporarily absent must retain its presentation.
    BrowserCatalog recovered(project.root);
    recovered.synchronize(assets);
    require(recovered.name(assets[1].id)=="Ancient Tree" && recovered.parent(assets[1].id)==models,
            "Temporarily missing managed assets lost their saved browser organization.");
    recovered.move(std::array{assets[1].id},{});
    require(recovered.parent(assets[1].id).empty() && recovered.name(assets[1].id)=="Ancient Tree",
            "Moving back to root discarded the asset display name.");
}

void atomicMoves() {
    TemporaryProject project;
    BrowserCatalog catalog(project.root);
    catalog.synchronize(assets);
    const auto source=catalog.createFolder({},"Source");
    const auto child=catalog.createFolder(source,"Child");
    const auto destination=catalog.createFolder({},"Destination");
    const auto duplicate=catalog.createFolder(destination,"Child");
    catalog.move(std::array{assets[1].id},child);
    const auto before=read(project.root/"assets/browser.json");
    rejects([&]{catalog.move(std::array{source},child);},"A folder was moved inside its own descendant.");
    rejects([&]{catalog.move(std::array{source},source);},"A folder was moved inside itself.");
    rejects([&]{catalog.move(std::array{assets[0].id,std::string("missing")},destination);},"A partly invalid batch was accepted.");
    rejects([&]{catalog.move(std::array{assets[0].id},"missing-folder");},"An unknown move destination was accepted.");
    rejects([&]{catalog.move(std::array{assets[0].id,child},destination);},"A conflicting folder move was accepted.");
    require(read(project.root/"assets/browser.json")==before && catalog.parent(child)==source && catalog.parent(assets[0].id).empty(),
            "A failed batch changed memory or published partial metadata.");
    require(catalog.parent(duplicate)==destination,"Failed move displaced an existing destination folder.");
    catalog.move(std::array{source,child,assets[1].id},destination);
    require(catalog.parent(source)==destination && catalog.parent(child)==source && catalog.parent(assets[1].id)==child,
            "Moving a selected parent plus its selected descendants flattened the hierarchy.");
    catalog.move(std::array{source},{});
    require(catalog.parent(source).empty() && catalog.parent(child)==source,"Returning a subtree to root changed descendant ownership.");
}

void invalidMetadataAndWriteFailure() {
    for (const auto* contents:{
        "{broken",
        R"({"version":99,"folders":[],"assets":{}})",
        R"({"version":1,"folders":[],"assets":[]})",
        R"({"version":1,"folders":[{"id":"folder:a","parent":"folder:b","name":"A"},{"id":"folder:b","parent":"folder:a","name":"B"}],"assets":{}})",
        R"({"version":1,"folders":[],"assets":{"asset":{"folder":"folder:absent","name":"Name"}}})"
    }) {
        TemporaryProject project;
        write(project.root/"assets/browser.json",contents);
        BrowserCatalog catalog(project.root);
        catalog.synchronize(assets);
        require(!catalog.status().empty() && catalog.items().size()==assets.size(),"Damaged metadata prevented the managed catalog from appearing.");
        rejects([&]{catalog.createFolder({},"New");},"Damaged metadata was overwritten by a folder creation.");
        rejects([&]{catalog.rename(assets[0].id,"Replacement");},"Damaged metadata was overwritten by a rename.");
        require(read(project.root/"assets/browser.json")==contents && catalog.name(assets[0].id)==assets[0].name,
                "Damaged browser data or in-memory names changed after a rejected edit.");
    }
    TemporaryProject project;
    BrowserCatalog catalog(project.root);
    catalog.synchronize(assets);
    fs::create_directory(project.root/"assets/browser.json"); // Destination cannot be atomically replaced with a file.
    rejects([&]{catalog.createFolder({},"Unpublished");},"A failed metadata write reported folder creation success.");
    rejects([&]{catalog.rename(assets[0].id,"Unpublished");},"A failed metadata write reported rename success.");
    require(catalog.items().size()==assets.size() && catalog.name(assets[0].id)==assets[0].name,
            "Failed metadata writes left unpublished names/folders in memory.");
}

void selectionPolicy() {
    const std::array<std::string,5> visible{"a","b","c","d","e"};
    BrowserSelection selection;
    selection.click("b",visible,false,false);
    selection.click("d",visible,true,false);
    require(selection.ids()==std::vector<std::string>({"b","d"}) && selection.primary()=="d","Ctrl did not add to selection.");
    selection.beginDrag("b");
    require(selection.ids().size()==2 && selection.primary()=="b","Dragging a selected card collapsed the selection.");
    selection.click("d",visible,true,false);
    require(selection.ids()==std::vector<std::string>{"b"},"Ctrl did not toggle a selected card off.");
    selection.click("a",visible,false,true);
    require(selection.ids()==std::vector<std::string>({"a","b","c","d"}) && selection.primary()=="a","Shift did not select the full anchored range.");
    selection.click("e",visible,true,true);
    require(selection.ids().size()==5,"Ctrl+Shift failed to extend selection additively.");
    selection.beginDrag("c");
    require(selection.ids().size()==5 && selection.primary()=="c","A group drag lost the other selected stable IDs.");
    selection.retain(std::array<std::string,2>{"e","a"});
    require(selection.ids().size()==2 && selection.contains("a") && selection.contains("e") && selection.contains(selection.primary()),
            "Changing visible rows retained hidden selections or an invalid primary.");
    selection.beginDrag("b");
    require(selection.ids()==std::vector<std::string>{"b"},"Dragging an unselected card did not start a new selection.");
    selection.click("missing",visible,false,false);
    require(selection.primary()=="b","An unknown row changed selection.");
    selection.set("");
    require(selection.ids().empty() && selection.primary().empty(),"Clearing selection retained a primary ID.");
}
}

int main() {
    try {
        metadataAndIdentity(); atomicMoves(); invalidMetadataAndWriteFailure(); selectionPolicy();
        std::cout<<"Asset browser metadata and selection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Asset browser test failed: "<<error.what()<<'\n';
        return 1;
    }
}
