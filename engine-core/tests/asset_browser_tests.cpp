#include "assets/BrowserCatalog.h"
#include "resources/AssetId.h"
#include "quader_bundle_fixture.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

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
    TemporaryProject() { fs::create_directories(root);initializeContentStorage(root); }
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
void populate(const fs::path& root,std::span<AssetEntry> entries) {
    for(auto& asset:entries) {
        const auto identity=canonicalAssetId(asset.id);
        const auto logical=fs::path(asset.id);
        auto file=root/"Content"/logical.filename();
        if(asset.kind==AssetKind::Model) {
            const auto token=logical.filename()=="manifest.json"?logical.parent_path().filename():logical.filename();
            file=root/"Content"/(token.string()+".crimson-model")/"manifest.json";
            write(file,"{}");write(file.parent_path()/"source.glb","managed content: "+identity);
        } else write(file,"managed content: "+identity);
        registerAssetPath(root,asset.id,file);
        asset.id=identity;
    }
}
void removeAssetFiles(const fs::path& root,std::span<const std::string> ids,const BrowserCatalog* catalog=nullptr) {
    for(const auto& id:ids)if(!catalog||!catalog->isFolder(id)) {
        auto file=resolveAssetPath(root,id);if(file.filename()=="manifest.json")file=file.parent_path();
        const auto relative=file.lexically_relative(root/"Content");
        require(!relative.empty()&&!relative.has_root_path()&&*relative.begin()!="..","A disposable deletion fixture escaped Content.");
        fs::remove_all(file);
    }
}
const std::array<AssetEntry,4> legacyAssets{{
    {"assets/textures/albedo.png","Albedo.png",AssetKind::Texture},
    {"assets/models/tree/manifest.json","Tree",AssetKind::Model},
    {"assets/scenes/level.json","Level",AssetKind::Scene},
    {"assets/textures/studio.hdr","Studio.hdr",AssetKind::Environment}
}};

void metadataAndIdentity() {
    auto assets=legacyAssets;
    TemporaryProject project;
    BrowserCatalog catalog(project.root);
    populate(project.root,assets);catalog.synchronize(assets);
    for(const auto& stored:storedAssetPaths(project.root))require(isAssetUuid(stored.id),"The physical location index emitted a path-shaped asset reference.");
    require(fs::is_regular_file(project.root/".crimson/browser.json")&&catalog.items().size()==2&&catalog.items()[0].id==BrowserCatalog::ContentFolder,
        "Physical Content/Engine roots or migrated metadata were not available.");
    require(catalog.items(BrowserCatalog::ContentFolder).size()==4 && catalog.items(BrowserCatalog::ContentFolder)[0].id==assets[0].id,"Initial catalog order is not alphabetical.");
    const auto art=catalog.createFolder(BrowserCatalog::ContentFolder,"  Art  ");
    require(isAssetUuid(art),"New browser directories did not receive stable UUID identities.");
    const auto models=catalog.createFolder(art,"Models");
    require(catalog.name(art)=="Art" && catalog.items(BrowserCatalog::ContentFolder)[0].id==art,"Folder name trimming or folder-first sorting failed.");
    rejects([&]{catalog.createFolder(BrowserCatalog::ContentFolder,"aRT");},"Case-insensitive sibling folder collision was accepted.");
    rejects([&]{catalog.createFolder(BrowserCatalog::ContentFolder,"../Outside");},"A path was accepted as a browser folder name.");
    rejects([&]{catalog.rename(art,"..");},"A traversal name was accepted during rename.");
    catalog.move(std::array{assets[1].id},models);
    catalog.move(std::array{assets[0].id,assets[3].id},art);
    catalog.rename(assets[1].id,"Ancient Tree");
    catalog.rename(art,"Environment Art");
    require(catalog.items(models).size()==1 && catalog.items(models)[0].id==assets[1].id && catalog.items(models)[0].name=="Ancient Tree",
            "Renaming/moving changed the stable asset identity or display name.");
    require(catalog.parent(assets[0].id)==art && catalog.parent(assets[3].id)==art,"Mixed assets did not move together.");
    const auto breadcrumbs=catalog.breadcrumbs(models);
    require(breadcrumbs.size()==3 && breadcrumbs[0].id==BrowserCatalog::ContentFolder && breadcrumbs[1].id==art && breadcrumbs[1].name=="Environment Art" && breadcrumbs[2].id==models,
            "Nested folder breadcrumbs did not follow a renamed parent.");
    for (const auto& asset:assets) {
        auto file=resolveAssetPath(project.root,asset.id);
        if(asset.kind==AssetKind::Model)file=file.parent_path()/"source.glb";
        require(read(file)=="managed content: "+asset.id,"Physical browser movement modified a managed source/package file.");
    }
    require(fs::is_directory(project.root/"Content/Environment Art/Models/Ancient Tree")&&
        resolveAssetPath(project.root,assets[0].id)==project.root/"Content/Environment Art/albedo.png",
        "Folder and asset rename/move did not change the actual backing locations.");
    catalog.rename(art,"environment art");
    bool exactCase=false;
    for(const auto& directory:fs::directory_iterator(project.root/"Content"))if(directory.path().filename()=="environment art")exactCase=true;
    require(exactCase&&catalog.folderPath(art)==project.root/"Content/environment art",
        "A case-only directory rename lost its requested casing or metadata path.");

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
    recovered.move(std::array{assets[1].id},BrowserCatalog::ContentFolder);
    require(recovered.parent(assets[1].id)==BrowserCatalog::ContentFolder && recovered.name(assets[1].id)=="Ancient Tree",
            "Moving back to root discarded the asset display name.");
}

void atomicMoves() {
    auto assets=legacyAssets;
    TemporaryProject project;
    BrowserCatalog catalog(project.root);
    populate(project.root,assets);catalog.synchronize(assets);
    const auto source=catalog.createFolder(BrowserCatalog::ContentFolder,"Source");
    const auto child=catalog.createFolder(source,"Child");
    const auto destination=catalog.createFolder(BrowserCatalog::ContentFolder,"Destination");
    const auto duplicate=catalog.createFolder(destination,"Child");
    catalog.move(std::array{assets[1].id},child);
    const auto before=read(project.root/".crimson/browser.json");
    rejects([&]{catalog.move(std::array{source},child);},"A folder was moved inside its own descendant.");
    rejects([&]{catalog.move(std::array{source},source);},"A folder was moved inside itself.");
    rejects([&]{catalog.move(std::array{assets[0].id,std::string("missing")},destination);},"A partly invalid batch was accepted.");
    rejects([&]{catalog.move(std::array{assets[0].id},"missing-folder");},"An unknown move destination was accepted.");
    rejects([&]{catalog.move(std::array{assets[0].id,child},destination);},"A conflicting folder move was accepted.");
    const auto reservedMissing=catalog.folderPath(destination)/"albedo.png";registerAssetPath(project.root,newAssetId(),reservedMissing);
    rejects([&]{catalog.move(std::array{assets[0].id},destination);},"A move reused another UUID's indexed but missing location.");
    require(read(project.root/".crimson/browser.json")==before && catalog.parent(child)==source && catalog.parent(assets[0].id)==BrowserCatalog::ContentFolder,
            "A failed batch changed memory or published partial metadata.");
    require(catalog.parent(duplicate)==destination,"Failed move displaced an existing destination folder.");
    catalog.move(std::array{source,child,assets[1].id},destination);
    require(catalog.parent(source)==destination && catalog.parent(child)==source && catalog.parent(assets[1].id)==child,
            "Moving a selected parent plus its selected descendants flattened the hierarchy.");
    catalog.move(std::array{source},BrowserCatalog::ContentFolder);
    require(catalog.parent(source)==BrowserCatalog::ContentFolder && catalog.parent(child)==source&&fs::is_directory(project.root/"Content/Source/Child"),"Returning a physical subtree to Content changed descendant ownership.");
    catalog.move(std::array{assets[0].id},destination,true);
    require(catalog.parent(assets[0].id)==destination&&resolveAssetPath(project.root,assets[0].id).filename()=="albedo 2.png"&&!fs::exists(reservedMissing),
        "Import placement did not suffix an indexed missing destination while retaining the asset UUID.");
}

void invalidMetadataAndWriteFailure() {
    auto assets=legacyAssets;
    for (const auto* contents:{
        "{broken",
        R"({"version":99,"folders":[],"assets":{}})",
        R"({"version":1,"folders":[],"assets":[]})",
        R"({"version":1,"folders":[{"id":"folder:a","parent":"folder:b","name":"A"},{"id":"folder:b","parent":"folder:a","name":"B"}],"assets":{}})",
        R"({"version":1,"folders":[],"assets":{"asset":{"folder":"folder:absent","name":"Name"}}})"
    }) {
        TemporaryProject project;assets=legacyAssets;
        write(project.root/".crimson/browser.json",contents);
        BrowserCatalog catalog(project.root);
        populate(project.root,assets);catalog.synchronize(assets);
        require(!catalog.status().empty() && catalog.items(BrowserCatalog::ContentFolder).size()==assets.size(),"Damaged metadata prevented the managed catalog from appearing.");
        rejects([&]{catalog.createFolder(BrowserCatalog::ContentFolder,"New");},"Damaged metadata was overwritten by a folder creation.");
        rejects([&]{catalog.rename(assets[0].id,"Replacement");},"Damaged metadata was overwritten by a rename.");
        require(read(project.root/".crimson/browser.json")==contents && catalog.name(assets[0].id)==assets[0].name,
                "Damaged browser data or in-memory names changed after a rejected edit.");
    }
    TemporaryProject project;assets=legacyAssets;
    BrowserCatalog catalog(project.root);
    populate(project.root,assets);catalog.synchronize(assets);
    fs::remove(project.root/".crimson/browser.json");fs::create_directory(project.root/".crimson/browser.json"); // Destination cannot be atomically replaced with a file.
    rejects([&]{catalog.createFolder(BrowserCatalog::ContentFolder,"Unpublished");},"A failed metadata write reported folder creation success.");
    rejects([&]{catalog.rename(assets[0].id,"Unpublished");},"A failed metadata write reported rename success.");
    require(catalog.items(BrowserCatalog::ContentFolder).size()==assets.size() && catalog.name(assets[0].id)==assets[0].name,
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

    std::vector<std::string> many;many.reserve(10000);
    for(unsigned i=0;i<10000;++i)many.push_back("asset-"+std::to_string(i));
    selection.setAll(many);
    require(selection.ids()==many&&selection.primary()==many.back(),"Select All changed stable ordering or its final primary.");
    for(const auto& id:many)require(selection.contains(id),"Large selection lost an indexed member.");
    selection.click(many[5000],many,true,false);
    require(selection.ids().size()==9999&&!selection.contains(many[5000]),"Ctrl toggle desynchronized large ordered selection membership.");
    selection.retain(std::span(many).subspan(2500,5000));
    require(selection.ids().size()==4999&&selection.ids().front()==many[2500]&&selection.ids().back()==many[7499]&&
        !selection.contains(many[0])&&!selection.contains(many[5000])&&selection.contains(selection.primary()),
        "Filtered large selection changed ordering, retained hidden members or lost its primary.");
    selection.setAll(selection.ids());
    require(selection.ids().size()==4999,"Select All invalidated its own borrowed input.");
    selection.set(selection.primary());
    require(selection.ids()==std::vector<std::string>{many[7499]},"Selecting the existing primary invalidated its borrowed ID.");
    selection.setAll(std::array<std::string,4>{"b","a","b",""});
    require(selection.ids()==std::vector<std::string>({"b","a"})&&selection.primary()=="a", "Select All retained duplicates/empty IDs or reordered its input.");
    selection.clear();
    require(!selection.contains("a")&&!selection.contains("b"),"Clear retained indexed selection members.");
}
void folderDeletion() {
    auto assets=legacyAssets;
    TemporaryProject project;BrowserCatalog catalog(project.root);populate(project.root,assets);catalog.synchronize(assets);
    const auto parent=catalog.createFolder(BrowserCatalog::ContentFolder,"Art"),child=catalog.createFolder(parent,"Models");
    catalog.move(std::array{assets[1].id},child);catalog.move(std::array{assets[0].id},parent);
    const auto nested=catalog.descendants(parent);require(nested.size()==3,"Folder deletion did not expand nested folders and assets.");
    const auto before=read(project.root/".crimson/browser.json");
    rejects([&]{catalog.erase(std::array{parent});},"Incomplete folder deletion was accepted.");
    require(read(project.root/".crimson/browser.json")==before&&catalog.contains(parent),"Failed folder deletion changed metadata.");
    auto all=nested;all.push_back(parent);const auto candidate=catalog.serializeWithout(all);
    require(read(project.root/".crimson/browser.json")==before&&!candidate.empty(),"Preparing deletion prematurely wrote browser metadata.");
    removeAssetFiles(project.root,all,&catalog);catalog.erase(all);require(!catalog.contains(parent)&&!catalog.contains(child)&&catalog.items(BrowserCatalog::ContentFolder).size()==2&&!fs::exists(project.root/"Content/Art"),"Complete folder deletion retained physical directories or cards.");
    BrowserCatalog reopened(project.root);reopened.synchronize(std::array{assets[2],assets[3]});require(reopened.items(BrowserCatalog::ContentFolder).size()==2,"Deleted folders reappeared on reopen.");
    const auto stale=reopened.createFolder(BrowserCatalog::ContentFolder,"Missing asset folder");populate(project.root,std::span(assets).first(1));reopened.synchronize(assets);reopened.move(std::array{assets[0].id},stale);
    reopened.synchronize(std::array{assets[2],assets[3]});removeAssetFiles(project.root,std::array{assets[0].id});reopened.erase(std::array{stale});
    BrowserCatalog final(project.root);final.synchronize(std::array{assets[2],assets[3]});require(final.status().empty(),"Missing asset's stale presentation corrupted metadata when deleting its folder.");
}
std::vector<std::string> itemIds(const std::vector<BrowserItem>& items,bool assetsOnly=false) {
    std::vector<std::string> result;
    for(const auto& item:items)if(!assetsOnly||!item.folder)result.push_back(item.id);
    return result;
}
void searchAndTypeQueries() {
    TemporaryProject project;BrowserCatalog catalog(project.root);
    std::array<AssetEntry,9> fixture{{
        {"assets/textures/OldStone_Albedo.PNG","Imported Stone OldStone_Albedo.PNG",AssetKind::Texture},
        {"assets/environment/EveningLight.HDR","Evening lighting",AssetKind::Environment},
        {"assets/models/AncientOak.GLB","Old oak",AssetKind::Model},
        {"assets/models/BrokenPillar.gltf","Pillar",AssetKind::Model},
        {"assets/models/package42/manifest.json","RuinWall.gltf",AssetKind::Model},
        {"assets/materials/stone.crimson-material","Stone Material",AssetKind::Material},
        {"assets/scenes/courtyard.crimson-scene","Courtyard",AssetKind::Scene},
        {"assets/particles/embers.crimson-particles","Campfire",AssetKind::Particle},
        {"assets/textures/stoneOutside.png","Outside Stone",AssetKind::Texture}
    }};
    populate(project.root,fixture);catalog.synchronize(fixture);
    const auto art=catalog.createFolder(BrowserCatalog::ContentFolder,"Art"),elsewhere=catalog.createFolder(BrowserCatalog::ContentFolder,"Elsewhere");
    const auto models=catalog.createFolder(art,"Models"),textures=catalog.createFolder(art,"Textures"),deep=catalog.createFolder(models,"Deeper");
    catalog.move(std::array{fixture[0].id,fixture[5].id},art);
    catalog.move(std::array{fixture[2].id,fixture[3].id},models);catalog.move(std::array{fixture[4].id},deep);
    catalog.move(std::array{fixture[1].id},textures);catalog.move(std::array{fixture[6].id,fixture[8].id},elsewhere);
    catalog.rename(fixture[0].id,"Floor Tint");catalog.rename(fixture[1].id,"Sky");
    catalog.rename(fixture[2].id,"Hero Tree");
    rejects([&]{catalog.rename(fixture[3].id,"Hero Tree");},"A physical package rename overwrote another package with the same name.");
    catalog.rename(fixture[3].id,"Hero Tree 2");
    catalog.rename(fixture[4].id,"Wall Assembly");catalog.rename(fixture[5].id,"Shared Shade");
    const auto metadata=read(project.root/".crimson/browser.json");
    const auto stamp=fs::last_write_time(project.root/".crimson/browser.json")-std::chrono::hours{1};fs::last_write_time(project.root/".crimson/browser.json",stamp);
    const auto revision=catalog.revision();
    const auto& cached=catalog.itemsView(art," HERO ",BrowserAssetFilter::Models);const auto* cachedData=cached.data();const auto cachedSize=cached.size();
    require(std::count_if(cached.begin(),cached.end(),[](const auto& item){return !item.folder;})==2,"Cached browser search omitted matching physical assets.");
    for(int frame=0;frame<100;++frame) {
        const auto& repeated=catalog.itemsView(art,"hero",BrowserAssetFilter::Models);
        require(repeated.data()==cachedData&&repeated.size()==cachedSize&&catalog.revision()==revision,
            "An unchanged browser frame rebuilt its cached query or changed catalog revision.");
    }

    const auto direct=catalog.items(art);
    require(direct.size()==4&&direct[0].id==models&&direct[1].id==textures&&itemIds(direct)==itemIds(catalog.items(art,"",BrowserAssetFilter::All)),
        "Inactive queries changed direct-child or folder-first catalog behavior.");
    require(itemIds(catalog.items(art," \t ",BrowserAssetFilter::All))==itemIds(direct),"Whitespace search unexpectedly enabled recursive filtering.");
    require(itemIds(catalog.items(art,"fLoOr",BrowserAssetFilter::All),true)==std::vector<std::string>{fixture[0].id},
        "Case-insensitive display alias search failed.");
    require(itemIds(catalog.items(art,"  oldstone_albedo.pNg  ",BrowserAssetFilter::Textures),true)==std::vector<std::string>{fixture[0].id},
        "Search lost the physical filename after aliasing or failed to trim its query.");
    require(itemIds(catalog.items(art,"imported",BrowserAssetFilter::All),true)==std::vector<std::string>{fixture[0].id},
        "An authored browser alias hid the original catalog name from search.");
    require(itemIds(catalog.items(art,"RUINWALL",BrowserAssetFilter::Models),true)==std::vector<std::string>{fixture[4].id},
        "Model search failed below nested folders when the package ID used manifest.json.");
    require(itemIds(catalog.items(art,"hero",BrowserAssetFilter::Models),true)==std::vector<std::string>({fixture[2].id,fixture[3].id}),
        "Model search did not retain alphabetical ordering after physical renames.");
    require(itemIds(catalog.items(art,"",BrowserAssetFilter::Models),true).size()==3,
        "Model filter omitted GLB/glTF/package models in descendant virtual folders.");
    require(itemIds(catalog.items(art,".hDr",BrowserAssetFilter::Textures),true)==std::vector<std::string>{fixture[1].id},
        "Texture filter omitted an HDR environment or did not search its filename.");
    const auto textureResults=itemIds(catalog.items(art,"",BrowserAssetFilter::Textures),true);
    require(textureResults==std::vector<std::string>({fixture[0].id,fixture[1].id}),"Recursive texture filtering included an asset from a sibling subtree.");
    require(itemIds(catalog.items(art,"hero",BrowserAssetFilter::Textures),true).empty(),"Text and asset-kind filters were not combined.");
    require(itemIds(catalog.items(art,"stone",BrowserAssetFilter::All),true)==std::vector<std::string>({fixture[0].id,fixture[5].id}),
        "Active folder search missed direct aliases/original names or escaped its subtree.");
    require(itemIds(catalog.items(BrowserCatalog::ContentFolder,"stone",BrowserAssetFilter::All),true).size()==3,"Root search failed to find matching assets across virtual folders.");
    for(const auto [filter,kind]:std::array{std::pair{BrowserAssetFilter::Materials,AssetKind::Material},std::pair{BrowserAssetFilter::Scenes,AssetKind::Scene},std::pair{BrowserAssetFilter::Particles,AssetKind::Particle}}) {
        const auto result=catalog.items(BrowserCatalog::ContentFolder,"",filter);
        require(std::count_if(result.begin(),result.end(),[](const auto& item){return !item.folder;})==1&&
            std::all_of(result.begin(),result.end(),[&](const auto& item){return item.folder||item.kind==kind;}),"A native asset filter returned another kind or lost a descendant.");
    }
    const auto empty=catalog.items(art,"no-such-asset",BrowserAssetFilter::Particles);
    require(empty.size()==2&&empty[0].folder&&empty[0].id==models&&empty[1].folder&&empty[1].id==textures,
        "A query hid direct navigation folders or included deeper folders as matching assets.");
    require(itemIds(catalog.items(models,"",BrowserAssetFilter::Models),true).size()==3&&catalog.items(deep).size()==1,
        "Filtering moved or duplicated assets between virtual folders.");
    const auto stable=itemIds(catalog.items(art,"",BrowserAssetFilter::All));
    catalog.synchronize(fixture);
    require(itemIds(catalog.items(art,"",BrowserAssetFilter::All))==stable&&read(project.root/".crimson/browser.json")==metadata&&fs::last_write_time(project.root/".crimson/browser.json")==stamp,
        "Queries/synchronization changed browser metadata, stable identity or ordering.");
    catalog.rename(fixture[0].id,"Updated floor");
    require(catalog.itemsView(art,"floor tint.png",BrowserAssetFilter::Textures).size()==2&&
        itemIds(catalog.itemsView(art,"updated floor.png",BrowserAssetFilter::Textures),true)==std::vector<std::string>{fixture[0].id},
        "Asset rename did not invalidate cached display/physical filename searches.");
    auto refreshed=fixture;refreshed[0].name="External Catalog Update";catalog.synchronize(refreshed);
    require(itemIds(catalog.itemsView(art,"external catalog update",BrowserAssetFilter::Textures),true)==std::vector<std::string>{fixture[0].id},
        "Catalog synchronization did not invalidate original-name search metadata.");
}
void filteredSelectionAndDeletion() {
    TemporaryProject project;BrowserCatalog catalog(project.root);
    std::array<AssetEntry,4> fixture{{
        {"visible-texture.png","Visible Texture",AssetKind::Texture},{"deep-texture.hdr","Deep Texture",AssetKind::Environment},
        {"hidden-model.glb","Hidden Model",AssetKind::Model},{"hidden-material.crimson-material","Hidden Material",AssetKind::Material}
    }};
    populate(project.root,fixture);catalog.synchronize(fixture);const auto folder=catalog.createFolder(BrowserCatalog::ContentFolder,"Navigation");
    catalog.move(std::array{fixture[1].id,fixture[2].id},folder);
    BrowserSelection selection;const auto all=itemIds(catalog.items(BrowserCatalog::ContentFolder,"",BrowserAssetFilter::All));
    selection.click(fixture[0].id,all,false,false);selection.click(fixture[3].id,all,true,false);
    const auto filtered=catalog.items(BrowserCatalog::ContentFolder,"",BrowserAssetFilter::Textures);
    const auto visible=itemIds(filtered);
    selection.retain(visible);
    require(selection.ids()==std::vector<std::string>{fixture[0].id}&&selection.primary()==fixture[0].id,
        "Applying a browser query retained hidden asset selection or lost a visible selected ID.");
    selection.clear();
    for(const auto& item:filtered)if(!item.folder)selection.click(item.id,visible,true,false);
    require(selection.ids().size()==2&&!selection.contains(folder)&&!selection.contains(fixture[2].id)&&!selection.contains(fixture[3].id),
        "Filtered bulk selection selected navigation folders or hidden assets.");
    const auto pending=catalog.prepareErase(selection.ids());
    require(!pending.contains(fixture[0].id)&&!pending.contains(fixture[1].id)&&pending.contains(folder)&&pending.contains(fixture[2].id)&&pending.contains(fixture[3].id),
        "Deleting filtered matches expanded into hidden folder contents or another asset kind.");
    removeAssetFiles(project.root,selection.ids());catalog.erase(selection.ids());catalog.synchronize(std::array{fixture[2],fixture[3]});
    selection.retain(itemIds(catalog.items(BrowserCatalog::ContentFolder,"",BrowserAssetFilter::Textures)));
    require(selection.ids().empty()&&selection.primary().empty()&&catalog.items(folder).size()==1,
        "Deleted filtered assets retained stale selection or removed unrelated nested assets.");
}
void physicalRootsAndPackageImages() {
    TemporaryProject project;BrowserCatalog catalog(project.root);
    const auto model=canonicalAssetId("assets/models/tree/manifest.json"),image=canonicalAssetId("assets/models/tree/images/bark.png"),image2=canonicalAssetId("assets/models/tree/images/nested/bark.png"),engine=canonicalAssetId("builtin:texture/gray");
    const auto package=project.root/"Content/Tree.crimson-model";
    write(package/"manifest.json","{}");write(package/"images/bark.png","immutable bark");
    write(package/"images/nested/bark.png","second immutable bark");
    registerAssetPath(project.root,model,package/"manifest.json");registerAssetPath(project.root,image,package/"images/bark.png");
    registerAssetPath(project.root,image2,package/"images/nested/bark.png");
    fs::create_directories(project.root/"Content/Discovered/Nested");
    const std::array fixture{AssetEntry{model,"Tree",AssetKind::Model},AssetEntry{image,"Bark.png",AssetKind::Texture},AssetEntry{engine,"Default gray",AssetKind::Texture},AssetEntry{image2,"Second Bark.png",AssetKind::Texture}};
    catalog.synchronize(fixture);
    require(catalog.writableFolder(BrowserCatalog::ContentFolder)&&!catalog.writableFolder(BrowserCatalog::EngineFolder)&&!catalog.writableFolder({})&&
        !catalog.mutableItem(BrowserCatalog::ContentFolder)&&!catalog.mutableItem(BrowserCatalog::EngineFolder)&&!catalog.mutableItem(engine),
        "Content/Engine root mutation boundaries are incorrect.");
    const auto content=catalog.items(BrowserCatalog::ContentFolder);
    const auto discovered=std::find_if(content.begin(),content.end(),[](const auto& item){return item.folder&&item.name=="Discovered";});
    require(discovered!=content.end()&&content.size()==4&&catalog.items(discovered->id).size()==1,
        "Real directories were hidden or model-package internals appeared as browser folders.");
    require(catalog.parent(model)==BrowserCatalog::ContentFolder&&catalog.parent(image)==BrowserCatalog::ContentFolder,
        "Model/image cards were placed inside an atomic package instead of beside it.");
    rejects([&]{catalog.rename(engine,"Edited");},"Engine content was renamed.");
    rejects([&]{catalog.move(std::array{image},BrowserCatalog::EngineFolder);},"A file was moved into Engine.");
    rejects([&]{catalog.createFolder(BrowserCatalog::EngineFolder,"New");},"A directory was created inside Engine.");
    rejects([&]{catalog.serializeWithout(std::array{engine});},"Engine content could be staged for deletion.");
    rejects([&]{catalog.createFolder(BrowserCatalog::ContentFolder,"CON");},"A reserved Windows directory name was accepted.");
    rejects([&]{catalog.rename(image,"bad:name");},"An invalid Windows filename was accepted.");
    const auto destination=catalog.createFolder(BrowserCatalog::ContentFolder,"Destination");
    write(catalog.folderPath(destination)/"bark.png","preexisting image");
    const auto reservedMissing=catalog.folderPath(destination)/"bark 2.png";registerAssetPath(project.root,newAssetId(),reservedMissing);
    catalog.move(std::array{model,image,image2},destination);
    const auto movedPackage=project.root/"Content/Destination/Tree.crimson-model";
    require(resolveAssetPath(project.root,model)==movedPackage/"manifest.json"&&resolveAssetPath(project.root,image).parent_path()==project.root/"Content/Destination"&&
        resolveAssetPath(project.root,image)!=resolveAssetPath(project.root,image2)&&read(project.root/"Content/Destination/bark.png")=="preexisting image"&&
        resolveAssetPath(project.root,image)!=reservedMissing&&resolveAssetPath(project.root,image2)!=reservedMissing&&!fs::exists(reservedMissing)&&
        read(movedPackage/"images/bark.png")=="immutable bark"&&read(resolveAssetPath(project.root,image))=="immutable bark"&&read(resolveAssetPath(project.root,image2))=="second immutable bark",
        "A package/image batch lost its retained import input, copied bytes or stable indexed identities.");
    catalog.rename(image,"Rough Bark");catalog.synchronize(fixture);
    require(resolveAssetPath(project.root,image).filename()=="Rough Bark.png"&&catalog.parent(image)==destination&&catalog.name(image)=="Rough Bark",
        "Renaming an extracted image did not retain its extension, location and stable card identity.");
    catalog.rename(destination,"Renamed");
    require(resolveAssetPath(project.root,model)==project.root/"Content/Renamed/Tree.crimson-model/manifest.json"&&
        resolveAssetPath(project.root,image)==project.root/"Content/Renamed/Rough Bark.png",
        "Directory rename did not update every indexed descendant.");
    const auto unavailable=newAssetId();const std::array missingFixture{fixture[0],fixture[1],fixture[2],AssetEntry{unavailable,"Pending Image",AssetKind::Texture}};
    catalog.synchronize(missingFixture);
    const auto missingResults=catalog.items(BrowserCatalog::ContentFolder,"Pending",BrowserAssetFilter::Textures);
    require(!catalog.mutableItem(unavailable)&&std::any_of(missingResults.begin(),missingResults.end(),[&](const auto& item){return item.id==unavailable;}),
        "An unavailable UUID card crashed the catalog, became writable or disappeared from name search.");
}
void bundledModelOwnership() {
    TemporaryProject project;const auto fixture=bundle_test::writeFixture(project.root);
    const auto cache=resolveAssetPath(project.root,fixture.modelId);const auto bundleBytes=read(fixture.file);
    BrowserCatalog catalog(project.root);const std::array assets{AssetEntry{fixture.modelId,"Triangle",AssetKind::Model}};
    catalog.synchronize(assets);
    const auto parent=catalog.parent(fixture.modelId);
    require(catalog.name(parent)=="Levels"&&catalog.items(parent).size()==1&&catalog.items(parent).front().id==fixture.modelId,
        "A bundled mesh was not presented beside its authored scene bundle.");
    require(!catalog.mutableItem(fixture.modelId),"A disposable bundle member was independently mutable.");
    require(catalog.deletableItem(fixture.modelId)&&!catalog.deletableItem(BrowserCatalog::ContentFolder)&&!catalog.deletableItem(BrowserCatalog::EngineFolder),
        "Bundle mesh deletion stayed blocked or allowed deleting protected roots.");
    rejects([&]{catalog.rename(fixture.modelId,"Renamed mesh");},"A bundle member was independently renamed.");
    rejects([&]{catalog.move(std::array{fixture.modelId},BrowserCatalog::ContentFolder);},"A bundle member was independently moved.");
    const auto deletion=catalog.prepareErase(std::array{fixture.modelId});
    require(!deletion.contains(fixture.modelId)&&catalog.contains(fixture.modelId)&&read(fixture.file)==bundleBytes&&fs::is_regular_file(cache),
        "Preparing a bundle mesh deletion failed or changed source files before the asset transaction.");
    catalog.rename(parent,"Renamed levels");catalog.synchronize(assets);
    const auto source=quaderBundleSource(project.root,fixture.modelId);
    require(source&&*source==project.root/"Content/Renamed levels/Bundle fixture.crimson-quader"&&read(*source)==bundleBytes&&
        resolveAssetPath(project.root,fixture.modelId)==cache&&catalog.parent(fixture.modelId)==parent,
        "Moving a bundle's Content folder changed a model identity/cache path or lost its authored owner.");
}
}

int main() {
    try {
        metadataAndIdentity(); atomicMoves(); invalidMetadataAndWriteFailure(); selectionPolicy();folderDeletion();searchAndTypeQueries();filteredSelectionAndDeletion();physicalRootsAndPackageImages();bundledModelOwnership();
        std::cout<<"Asset browser metadata and selection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Asset browser test failed: "<<error.what()<<'\n';
        return 1;
    }
}
