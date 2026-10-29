#include "native_asset_retention.hpp"

#include "editor/session/project_session.hpp"
#include "io/project/project_file_system.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace crimson::integration {
namespace {
std::uint64_t combine(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
}
std::string asset_uuid(const std::string& text) {
    const auto parsed = quader::ProjectAssetId::parse(text);
    if (!parsed) throw std::runtime_error("A native scene contains an invalid Engine asset reference.");
    return std::string(parsed->str());
}
}

struct NativeAssetRetention::State {
    struct Root {
        std::vector<std::string> ids;
        std::string owner_id;
        std::string owner_name;
        bool complete = false;
    };
    quader::ProjectSession& project;
    std::uint64_t project_generation;
    std::uint64_t observed_revision = 1u;
    std::map<std::weak_ptr<const std::string>, Root,
             std::owner_less<std::weak_ptr<const std::string>>> roots;

    explicit State(quader::ProjectSession& value)
        : project(value), project_generation(value.project_generation()) {}
    void reset_project() {
        if (project_generation == project.project_generation()) return;
        roots.clear();
        project_generation = project.project_generation();
        ++observed_revision;
    }
    void retain(const quader::EngineComponentsComponent& components,
                const std::string& owner_id, const std::string& owner_name) {
        if (!quader::engine_components_is_valid(components))
            throw std::runtime_error("A native scene contains invalid Engine component metadata.");
        auto [entry, inserted] = roots.try_emplace(components.serialized_components);
        if (inserted) {
            auto& root = entry->second;
            root.owner_id = owner_id;
            root.owner_name = owner_name;
            root.complete = components.asset_references_complete;
            for (const auto& id : components.asset_references) root.ids.push_back(asset_uuid(id));
            ++observed_revision;
        } else {
            // Equal immutable bytes may be shared by multiple native envelopes.
            // Keep the union; incomplete metadata can never become a proof of no use.
            auto& root = entry->second;
            root.complete = root.complete && components.asset_references_complete;
            for (const auto& id : components.asset_references) {
                auto canonical = asset_uuid(id);
                if (std::ranges::find(root.ids, canonical) == root.ids.end()) {
                    root.ids.push_back(std::move(canonical));
                    ++observed_revision;
                }
            }
        }
    }
    void prune() {
        for (auto entry = roots.begin(); entry != roots.end();) {
            if (entry->first.expired()) { entry = roots.erase(entry); ++observed_revision; }
            else ++entry;
        }
    }
};

NativeAssetRetention::NativeAssetRetention(quader::ProjectSession& project)
    : state_(std::make_shared<State>(project)) {
    for (const auto& scene : project.open_scenes()) if (scene && scene->session) observe(scene->session->scene());
}
NativeAssetRetention::~NativeAssetRetention() = default;

void NativeAssetRetention::observe(quader::SceneDocument& scene) {
    auto& state = *state_;
    state.reset_project();
    state.prune();
    std::string owner_id = "native-scene";
    std::string owner_name = "Native scene";
    for (const auto& resident : state.project.open_scenes())
        if (resident && resident->session && &resident->session->scene() == &scene) {
            owner_id = std::string(resident->scene_id.str());
            owner_name = resident->display_name;
            break;
        }
    const auto generation = state.project_generation;
    const std::weak_ptr<State> weak = state_;
    scene.set_engine_components_observer([weak, generation, owner_id, owner_name](const auto& value) {
        if (auto current = weak.lock(); current && current->project_generation == generation &&
            current->project.project_generation() == generation)
            current->retain(value, owner_id, owner_name);
    });
    for (const auto entity : scene.entities())
        if (const auto* value = scene.engine_components(entity)) state.retain(*value, owner_id, owner_name);
}

std::vector<AssetUsage> NativeAssetRetention::snapshot() {
    auto& state = *state_;
    state.reset_project();
    std::vector<AssetUsage> result;
    std::unordered_set<const std::string*> current_payloads;
    for (const auto& resident : state.project.open_scenes()) {
        if (!resident || !resident->session) continue;
        auto& scene = resident->session->scene();
        observe(scene);
        for (const auto entity : scene.entities()) {
            const auto* value = scene.engine_components(entity);
            if (!value) continue;
            if (!value->asset_references_complete)
                throw std::runtime_error("Native scene Engine asset references are incomplete; deletion cannot be validated.");
            current_payloads.insert(value->serialized_components.get());
            for (const auto& id : value->asset_references)
                result.push_back({asset_uuid(id), std::string(resident->scene_id.str()),
                    resident->display_name, "Native scene component", true, false, false, entity});
        }
    }
    state.prune();
    for (const auto& [payload, root] : state.roots) {
        const auto pin = payload.lock();
        if (!pin || current_payloads.contains(pin.get())) continue;
        if (!root.complete)
            throw std::runtime_error("Native scene history contains incomplete Engine asset references; deletion cannot be validated.");
        for (const auto& id : root.ids)
            result.push_back({id, root.owner_id, root.owner_name, "Native undo / redo history", true, true});
    }
    if (!state.project.has_project()) return result;
    const auto opened = quader::ProjectFileSystem::open(state.project.root());
    if (!opened) throw std::runtime_error(opened.error().message);
    const auto file_system = opened.value().begin_resolution_batch();
    std::filesystem::recursive_directory_iterator files(file_system.root());
    for (const auto& entry : files) {
        const auto name = entry.path().filename().string();
        if (!name.empty() && name.front() == '.') {
            if (entry.is_directory()) files.disable_recursion_pending();
            continue;
        }
        const auto relative = quader::ProjectRelativePath::parse(entry.path().lexically_relative(file_system.root()));
        if (!relative) throw std::runtime_error(relative.error().message);
        // Resolve directories too, before iterator recursion can enter a junction.
        const auto contained = file_system.resolve(relative.value());
        if (!contained) throw std::runtime_error(contained.error().message);
        if (!entry.is_regular_file()) continue;
        auto extension = entry.path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".qdr") continue;
        const auto path = file_system.resolve_existing_file(relative.value());
        if (!path) throw std::runtime_error(path.error().message);
        const auto saved = quader::QdrDocumentReader::read_project_engine_asset_dependencies(path.value());
        if (!saved.ok || !saved.engine_asset_ids_complete)
            throw std::runtime_error("Cannot validate native scene asset references in '" + relative.value().portable_string() +
                "': " + (saved.message.empty() ? "Engine reference inventory is incomplete." : saved.message));
        for (const auto& id : saved.engine_asset_ids)
            result.push_back({std::string(id.str()), std::string(quader::SceneId::from_bytes(saved.scene_uuid).str()),
                saved.scene_name, "Saved native scene component", false, false});
    }
    return result;
}

std::uint64_t NativeAssetRetention::revision() const {
    const auto& state = *state_;
    auto result = combine(state.observed_revision, state.project.project_generation());
    result = combine(result, state.project.activation_generation());
    for (const auto& resident : state.project.open_scenes()) if (resident && resident->session) {
        result = combine(result, resident->session_id.packed());
        result = combine(result, resident->session->document_binding_generation());
        result = combine(result, resident->session->scene().content_revision());
        result = combine(result, resident->session->history().revision());
    }
    return result;
}
}
