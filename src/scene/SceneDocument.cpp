#include "SceneDocument.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
using Json = nlohmann::json;
std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
uint64_t randomId() {
    static std::mt19937_64 random([] {
        std::random_device device;
        std::seed_seq seed{device(),device(),device(),device(),device(),device(),device(),device()};
        return std::mt19937_64(seed);
    }());
    uint64_t result{};
    while (!result) result = random();
    return result;
}
std::string hexId(uint64_t id) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << id;
    return text.str();
}
EntityId parseId(const Json& value) {
    if (!value.is_string()) throw std::runtime_error("Scene entity IDs must be hexadecimal strings.");
    const auto text = value.get<std::string>();
    EntityId result{};
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),result,16);
    if (text.size() != 16 || parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size())
        throw std::runtime_error("Scene has an invalid entity ID.");
    return result;
}
std::string uniqueToken() { return hexId(randomId()) + hexId(randomId()); }
std::filesystem::path scenePath(const std::filesystem::path& root, const std::string& id) {
    const auto relative = fromUtf8(id);
    if (relative.has_root_path() || relative.extension() != ".crimson-scene" ||
        relative.parent_path() != std::filesystem::path("assets/scenes"))
        throw std::runtime_error("Scene asset must be a .crimson-scene file directly under assets/scenes.");
    const auto directory = std::filesystem::weakly_canonical(root / "assets/scenes");
    const auto path = std::filesystem::weakly_canonical(root / relative);
    if (path.parent_path() != directory)
        throw std::runtime_error("Scene asset path escapes its managed directory.");
    // Also reject a redirected assets/scenes directory outside the project.
    const auto canonicalRoot = std::filesystem::weakly_canonical(root);
    const auto contained = directory.lexically_relative(canonicalRoot);
    if (contained.empty() || contained.is_absolute() || *contained.begin() == "..")
        throw std::runtime_error("Managed scene directory escapes the project.");
    return path;
}
uint32_t nextOrder(const std::vector<EntityRecord>& records, EntityId parent) {
    uint64_t result{};
    for (const auto& record : records) if (record.parent == parent)
        result = std::max(result,static_cast<uint64_t>(record.order)+1);
    if (result > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Scene sibling order is exhausted.");
    return static_cast<uint32_t>(result);
}
EntityId unusedId(const std::vector<EntityRecord>& records) {
    EntityId result{};
    do { result = randomId(); }
    while (std::any_of(records.begin(),records.end(),[result](const auto& record) { return record.id == result; }));
    return result;
}
template<class Light> Json lightJson(const Light& light) {
    return { {"color",light.color}, {"intensity",light.intensity}, {"enabled",light.enabled} };
}
template<class Light> Light readLight(const Json& json) {
    Light result;
    result.color = json.at("color").get<Vec3>();
    result.intensity = json.at("intensity").get<float>();
    result.enabled = json.at("enabled").get<bool>();
    return result;
}
struct StoredScene {
    std::string name;
    std::vector<EntityRecord> records;
    SceneProperties properties;
};
StoredScene readScene(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (!size || size > 64u*1024u*1024u) throw std::runtime_error("Scene file is empty or larger than 64 MiB.");
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read scene asset.");
    const Json json = Json::parse(input);
    if (json.at("version") != 1 || json.at("type") != "crimson-scene")
        throw std::runtime_error("Unsupported scene file type or version.");
    StoredScene result;
    result.name = json.at("name").get<std::string>();
    if (result.name.empty() || result.name.size() > 1024) throw std::runtime_error("Scene name is invalid.");
    const auto& settings = json.at("settings");
    result.properties.exposure = settings.at("exposure").get<float>();
    const auto& environment = settings.at("environment");
    result.properties.environment.map = environment.at("map").get<std::string>();
    result.properties.environment.enabled = environment.at("enabled").get<bool>();
    result.properties.environment.intensity = environment.at("intensity").get<float>();
    result.properties.environment.rotation = environment.at("rotation").get<float>();
    const auto& entities = json.at("entities");
    if (!entities.is_array() || entities.size() > 100000) throw std::runtime_error("Invalid scene entity array.");
    result.records.reserve(entities.size());
    for (const auto& entity : entities) {
        EntityRecord record;
        record.id = parseId(entity.at("id"));
        record.name = entity.at("name").get<std::string>();
        record.parent = parseId(entity.at("parent"));
        const auto& order = entity.at("order");
        if (!order.is_number_unsigned() || order.get<uint64_t>() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Scene has an invalid sibling order.");
        record.order = order.get<uint32_t>();
        record.local = entity.at("local").get<Mat4>();
        if (entity.contains("model")) record.model = entity.at("model").get<std::string>();
        if (entity.contains("directional")) record.directional = readLight<SceneDirectionalLight>(entity.at("directional"));
        if (entity.contains("point")) record.point = readLight<ScenePointLight>(entity.at("point"));
        result.records.push_back(std::move(record));
    }
    Scene::validate(result.records,result.properties);
    return result;
}
Json writeScene(const std::string& name, const Scene& scene) {
    const auto& properties = scene.properties();
    const auto& environment = properties.environment;
    Json entities = Json::array();
    for (const auto& record : scene.records()) {
        Json entity{{"id",hexId(record.id)}, {"name",record.name}, {"parent",hexId(record.parent)},
            {"order",record.order}, {"local",record.local}};
        if (record.model) entity["model"] = *record.model;
        if (record.directional) entity["directional"] = lightJson(*record.directional);
        if (record.point) entity["point"] = lightJson(*record.point);
        entities.push_back(std::move(entity));
    }
    return {{"version",1}, {"type","crimson-scene"}, {"name",name}, {"entities",std::move(entities)},
        {"settings",{{"exposure",properties.exposure},{"environment",{{"map",environment.map},
        {"enabled",environment.enabled},{"intensity",environment.intensity},{"rotation",environment.rotation}}}}}};
}
SceneCommand difference(const std::string& label, const std::vector<EntityRecord>& before,
                        const std::vector<EntityRecord>& after, const SceneProperties& beforeProperties,
                        const SceneProperties& afterProperties, const SceneSelection& beforeSelection,
                        const SceneSelection& afterSelection) {
    SceneCommand command;
    command.label = label;
    command.beforeSelection = beforeSelection;
    command.afterSelection = afterSelection;
    std::unordered_map<EntityId,const EntityRecord*> previous;
    for (const auto& record : before) previous.emplace(record.id,&record);
    for (const auto& record : after) {
        const auto found = previous.find(record.id);
        if (found == previous.end()) command.entities.push_back({record.id,std::nullopt,record});
        else {
            if (!sameEntity(*found->second,record)) command.entities.push_back({record.id,*found->second,record});
            previous.erase(found);
        }
    }
    for (const auto& [id,record] : previous) command.entities.push_back({id,*record,std::nullopt});
    std::sort(command.entities.begin(),command.entities.end(),[](const auto& a,const auto& b) { return a.id < b.id; });
    if (!sameProperties(beforeProperties,afterProperties)) {
        command.beforeProperties = beforeProperties; command.afterProperties = afterProperties;
    }
    return command;
}
}

std::vector<SceneAssetEntry> scanSceneAssets(const std::filesystem::path& root, std::vector<std::string>* diagnostics) {
    std::vector<SceneAssetEntry> entries;
    try {
        const auto directory = root / "assets/scenes";
        if (!std::filesystem::exists(directory)) return entries;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".crimson-scene") continue;
            const auto id = "assets/scenes/" + utf8(entry.path().filename());
            try { entries.push_back({id,readScene(scenePath(root,id)).name}); }
            catch (const std::exception& error) { if (diagnostics) diagnostics->push_back(id + ": " + error.what()); }
        }
    } catch (const std::exception& error) { if (diagnostics) diagnostics->push_back(error.what()); }
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    return entries;
}
bool deleteSceneAsset(const std::filesystem::path& root, const std::string& id, std::string& error) {
    try {
        const auto path = scenePath(root,id);
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Scene asset no longer exists.");
        if (!std::filesystem::remove(path)) throw std::runtime_error("Could not delete scene asset.");
        error.clear();
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

std::string UndoRedoManager::undoLabel() const { return canUndo() ? commands_[cursor_-1].label : std::string{}; }
std::string UndoRedoManager::redoLabel() const { return canRedo() ? commands_[cursor_].label : std::string{}; }
void UndoRedoManager::reset(bool saved) {
    commands_.clear(); cursor_ = 0;
    state_ = nextState_++;
    savedState_ = saved ? state_ : 0;
}
void UndoRedoManager::push(SceneCommand command) {
    command.beforeState = state_;
    command.afterState = nextState_++;
    commands_.erase(commands_.begin()+static_cast<std::ptrdiff_t>(cursor_),commands_.end());
    commands_.push_back(std::move(command));
    state_ = commands_.back().afterState;
    if (commands_.size() > MaximumEntries) commands_.erase(commands_.begin());
    cursor_ = commands_.size();
}

SceneDocument::SceneDocument(std::filesystem::path projectRoot) : projectRoot_(std::move(projectRoot)) { newScene(); }
void SceneDocument::sanitizeSelection() {
    std::unordered_set<EntityId> seen;
    std::erase_if(selection_.ids,[&](EntityId id) { return !scene_.contains(id) || !seen.insert(id).second; });
    if (std::find(selection_.ids.begin(),selection_.ids.end(),selection_.primary) == selection_.ids.end())
        selection_.primary = selection_.ids.empty() ? NoEntity : selection_.ids.back();
}
void SceneDocument::select(std::vector<EntityId> ids, EntityId primary) {
    if (transaction_) return;
    selection_ = {std::move(ids),primary}; sanitizeSelection();
}
bool SceneDocument::dirty() const {
    if (history_.dirty()) return true;
    if (!transaction_) return false;
    if (!sameProperties(transaction_->properties,scene_.properties())) return true;
    const auto current = scene_.records();
    if (current.size() != transaction_->before.size()) return true;
    for (size_t index=0;index<current.size();++index)
        if (!sameEntity(current[index],transaction_->before[index])) return true;
    return false;
}
void SceneDocument::newScene() {
    scene_.apply({},SceneProperties{});
    history_.reset(false); transaction_.reset(); selection_ = {};
    assetId_.clear(); name_ = "Untitled"; status_ = "New unsaved scene.";
}
bool SceneDocument::load(const std::string& id) {
    try {
        auto stored = readScene(scenePath(projectRoot_,id));
        std::string preparedId = id;
        scene_.apply(stored.records,stored.properties);
        transaction_.reset(); history_.reset(true); selection_ = {};
        assetId_ = std::move(preparedId); name_ = std::move(stored.name);
        status_ = "Opened scene: " + name_;
        return true;
    } catch (const std::exception& error) { status_ = "Could not open scene: " + std::string(error.what()); return false; }
}
bool SceneDocument::save(const std::string& name, bool saveAs) {
    if (transaction_ && !commit()) return false;
    try {
        std::string savedName = name.empty() ? name_ : name;
        if (savedName.empty() || savedName.size() > 1024 || savedName.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error("Enter a scene name containing 1 to 1024 UTF-8 bytes.");
        std::string savedId = assetId_;
        if (savedId.empty() || saveAs) {
            do { savedId = "assets/scenes/" + uniqueToken() + ".crimson-scene"; }
            while (std::filesystem::exists(scenePath(projectRoot_,savedId)));
        }
        const auto contents = writeScene(savedName,scene_).dump(2) + "\n";
        writeFileAtomically(scenePath(projectRoot_,savedId),contents);
        assetId_ = std::move(savedId); name_ = std::move(savedName);
        history_.savedState_ = history_.state_;
        status_ = "Saved scene: " + name_;
        return true;
    } catch (const std::exception& error) { status_ = "Could not save scene: " + std::string(error.what()); return false; }
}
bool SceneDocument::begin(const std::string& label) {
    if (transaction_) { status_ = "Finish the active edit first."; return false; }
    transaction_ = Transaction{label,scene_.records(),scene_.properties(),selection_};
    status_.clear(); return true;
}
bool SceneDocument::update(const Mutation& mutation) {
    if (!transaction_) { status_ = "There is no active edit."; return false; }
    try {
        auto records = scene_.records(); auto properties = scene_.properties();
        mutation(records,properties);
        scene_.apply(records,properties);
        sanitizeSelection(); status_.clear(); return true;
    } catch (const std::exception& error) { status_ = error.what(); return false; }
}
bool SceneDocument::commit() {
    if (!transaction_) return true;
    auto command = difference(transaction_->label,transaction_->before,scene_.records(),transaction_->properties,
                              scene_.properties(),transaction_->selection,selection_);
    if (!command.entities.empty() || command.beforeProperties) history_.push(std::move(command));
    transaction_.reset(); return true;
}
void SceneDocument::cancel() {
    if (!transaction_) return;
    scene_.apply(transaction_->before,transaction_->properties);
    selection_ = transaction_->selection; transaction_.reset(); sanitizeSelection();
}
bool SceneDocument::edit(const std::string& label, const Mutation& mutation) {
    if (!begin(label)) return false;
    if (!update(mutation)) { cancel(); return false; }
    return commit();
}
bool SceneDocument::applyHistory(const SceneCommand& command, bool forward) {
    try {
        auto records = scene_.records(); auto properties = scene_.properties();
        for (const auto& change : command.entities) {
            std::erase_if(records,[&](const auto& record) { return record.id == change.id; });
            const auto& restored = forward ? change.after : change.before;
            if (restored) records.push_back(*restored);
        }
        const auto& restoredProperties = forward ? command.afterProperties : command.beforeProperties;
        if (restoredProperties) properties = *restoredProperties;
        scene_.apply(records,properties);
        selection_ = forward ? command.afterSelection : command.beforeSelection;
        sanitizeSelection(); status_.clear(); return true;
    } catch (const std::exception& error) { status_ = error.what(); return false; }
}
bool SceneDocument::undo() {
    if (transaction_ || !history_.canUndo()) return false;
    const auto& command = history_.commands_[history_.cursor_-1];
    if (!applyHistory(command,false)) return false;
    history_.state_ = command.beforeState; --history_.cursor_; return true;
}
bool SceneDocument::redo() {
    if (transaction_ || !history_.canRedo()) return false;
    const auto& command = history_.commands_[history_.cursor_];
    if (!applyHistory(command,true)) return false;
    history_.state_ = command.afterState; ++history_.cursor_; return true;
}
EntityId SceneDocument::create(EntityRecord record, const std::string& label) {
    if (!begin(label)) return NoEntity;
    record.id = unusedId(scene_.records());
    const EntityId id = record.id;
    if (!update([&](auto& records,auto&) { record.order = nextOrder(records,record.parent); records.push_back(record); })) {
        cancel(); return NoEntity;
    }
    selection_ = {{id},id}; commit(); return id;
}
EntityId SceneDocument::createEmpty(const std::string& name) {
    EntityRecord record; record.name = name; return create(std::move(record),"Create entity");
}
EntityId SceneDocument::createModel(const std::string& assetId, const std::string& name, const Mat4& local) {
    EntityRecord record; record.name = name; record.model = assetId; record.local = local;
    return create(std::move(record),"Place model");
}
std::vector<EntityId> SceneDocument::createModels(const std::vector<ModelPlacement>& models) {
    if (models.empty() || !begin("Place models")) return {};
    std::vector<EntityId> ids;
    if (!update([&](auto& records,auto&) {
        for (const auto& model : models) {
            EntityRecord record;
            record.id=unusedId(records); record.name=model.name; record.model=model.assetId; record.local=model.local;
            record.order=nextOrder(records,NoEntity); ids.push_back(record.id); records.push_back(std::move(record));
        }
    })) { cancel(); return {}; }
    selection_={ids,ids.back()};
    commit();
    return ids;
}
EntityId SceneDocument::createDirectionalLight(const std::string& name) {
    EntityRecord record; record.name = name; record.directional.emplace();
    return create(std::move(record),"Create directional light");
}
EntityId SceneDocument::createPointLight(const std::string& name) {
    EntityRecord record; record.name = name; record.point.emplace();
    return create(std::move(record),"Create point light");
}
bool SceneDocument::updateEntity(EntityId id, const EntityRecord& record, const std::string& label) {
    return edit(label,[&](auto& records,auto&) {
        const auto found = std::find_if(records.begin(),records.end(),[&](const auto& value) { return value.id == id; });
        if (found == records.end() || record.id != id) throw std::runtime_error("Entity no longer exists or its ID changed.");
        *found = record;
    });
}
bool SceneDocument::eraseSelection() {
    const auto roots = scene_.topmost(selection_.ids);
    if (roots.empty() || !begin("Delete entities")) return false;
    std::unordered_set<EntityId> removed;
    for (auto root : roots) for (auto id : scene_.subtree(root)) removed.insert(id);
    if (!update([&](auto& records,auto&) {
        std::erase_if(records,[&](const auto& record) { return removed.contains(record.id); });
    })) { cancel(); return false; }
    selection_ = {}; return commit();
}
bool SceneDocument::duplicateSelection() {
    const auto roots = scene_.topmost(selection_.ids);
    if (roots.empty() || !begin("Duplicate entities")) return false;
    const auto previousSelection = selection_;
    std::vector<EntityId> originals;
    for (auto root : roots) {
        const auto branch = scene_.subtree(root); originals.insert(originals.end(),branch.begin(),branch.end());
    }
    std::unordered_map<EntityId,EntityId> copied;
    if (!update([&](auto& records,auto&) {
        for (auto original : originals) {
            auto record = *scene_.find(original);
            record.id = unusedId(records); copied.emplace(original,record.id);
            if (const auto parent = copied.find(record.parent); parent != copied.end()) record.parent = parent->second;
            else record.name += " Copy";
            record.order = nextOrder(records,record.parent);
            records.push_back(std::move(record));
        }
    })) { cancel(); return false; }
    selection_ = {};
    for (auto original : previousSelection.ids) if (copied.contains(original)) selection_.ids.push_back(copied.at(original));
    if (copied.contains(previousSelection.primary)) selection_.primary = copied.at(previousSelection.primary);
    sanitizeSelection(); return commit();
}
bool SceneDocument::reparentSelection(EntityId parent) {
    const auto roots = scene_.topmost(selection_.ids);
    if (roots.empty()) return false;
    if (parent && !scene_.contains(parent)) { status_ = "Parent entity no longer exists."; return false; }
    const auto parentWorld = parent ? scene_.worldTransform(parent) : std::optional<Mat4>(Mat4{});
    const auto parentInverse = parentWorld ? math::inverse(*parentWorld) : std::nullopt;
    if (!parentInverse) { status_ = "Cannot reparent under a singular transform."; return false; }
    std::unordered_map<EntityId,Mat4> locals;
    for (auto root : roots) {
        const auto branch = scene_.subtree(root);
        if (std::find(branch.begin(),branch.end(),parent) != branch.end()) {
            status_ = "Cannot parent an entity under itself or a descendant."; return false;
        }
        const auto world = scene_.worldTransform(root);
        if (!world) { status_ = "Cannot determine the entity world transform."; return false; }
        locals.emplace(root,*parentInverse * *world);
    }
    return edit("Reparent entities",[&](auto& records,auto&) {
        for (auto root : roots) {
            const auto order = nextOrder(records,parent);
            auto& record = *std::find_if(records.begin(),records.end(),[root](const auto& value) { return value.id == root; });
            record.parent = parent; record.order = order; record.local = locals.at(root);
        }
    });
}
bool SceneDocument::setProperties(const SceneProperties& properties, const std::string& label) {
    return edit(label,[&](auto&,auto& current) { current = properties; });
}

bool SceneDocument::groupSelection(const std::string& name) {
    const auto roots=scene_.topmost(selection_.ids);
    if (roots.empty()) return false;
    EntityId parent=scene_.find(roots.front())->parent;
    Vec3 center{};
    std::unordered_map<EntityId,Mat4> worlds;
    for (const auto id : roots) {
        const auto record=scene_.find(id);
        const auto world=scene_.worldTransform(id);
        if (!record || !world) { status_="Cannot resolve the selected entities."; return false; }
        if (record->parent != parent) parent=NoEntity;
        center=center+math::transformPoint(*world,{0,0,0})/static_cast<double>(roots.size());
        worlds.emplace(id,*world);
    }
    const auto parentWorld=parent ? scene_.worldTransform(parent) : std::optional<Mat4>{Mat4{}};
    const auto parentInverse=parentWorld ? math::inverse(*parentWorld) : std::nullopt;
    const auto groupWorld=math::translation(center);
    const auto groupInverse=math::inverse(groupWorld);
    if (!parentInverse || !groupInverse) { status_="Cannot group under a singular transform."; return false; }
    if (!begin("Group entities")) return false;
    EntityRecord group;
    group.id=unusedId(scene_.records()); group.name=name; group.parent=parent;
    group.local=*parentInverse*groupWorld;
    if (!update([&](auto& records,auto&) {
        group.order=nextOrder(records,parent); records.push_back(group);
        uint32_t order=0;
        for (const auto id : roots) {
            auto& record=*std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==id;});
            record.parent=group.id; record.order=order++; record.local=*groupInverse*worlds.at(id);
        }
    })) { cancel(); return false; }
    selection_={{group.id},group.id};
    return commit();
}

bool SceneDocument::ungroupSelection() {
    std::vector<EntityId> groups;
    for (const auto id : scene_.topmost(selection_.ids)) {
        const auto record=scene_.find(id);
        if (record && !record->model && !record->directional && !record->point && !scene_.children(id).empty()) groups.push_back(id);
    }
    if (groups.empty()) { status_="Select a group to ungroup."; return false; }
    struct ChildMove { EntityId id,parent; Mat4 local; };
    std::vector<ChildMove> moves;
    for (const auto group : groups) {
        const auto parent=scene_.find(group)->parent;
        const auto world=parent ? scene_.worldTransform(parent) : std::optional<Mat4>{Mat4{}};
        const auto inverse=world ? math::inverse(*world) : std::nullopt;
        if (!inverse) { status_="Cannot ungroup under a singular transform."; return false; }
        for (const auto id : scene_.children(group)) {
            const auto childWorld=scene_.worldTransform(id);
            if (!childWorld) { status_="Cannot resolve the child transform."; return false; }
            moves.push_back({id,parent,*inverse * *childWorld});
        }
    }
    if (!begin("Ungroup entities")) return false;
    if (!update([&](auto& records,auto&) {
        for (const auto& move : moves) {
            const auto order=nextOrder(records,move.parent);
            auto& record=*std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==move.id;});
            record.parent=move.parent; record.order=order; record.local=move.local;
        }
        std::erase_if(records,[&](const auto& r){return std::find(groups.begin(),groups.end(),r.id)!=groups.end();});
    })) { cancel(); return false; }
    selection_={};
    for (const auto& move : moves) selection_.ids.push_back(move.id);
    sanitizeSelection();
    return commit();
}

} // namespace crimson
