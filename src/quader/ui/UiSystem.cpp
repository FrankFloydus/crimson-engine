#include "quader/ui/UiSystem.h"

#include "quader/core/Logger.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace quader {
namespace {

constexpr float RadToDeg = 57.29577951308232f;
constexpr float DegToRad = 0.017453292519943295f;

void copyNameToBuffer(const NameComponent* name, std::array<char, 128>& buffer) {
    buffer.fill('\0');
    if (!name) {
        return;
    }
    std::snprintf(buffer.data(), buffer.size(), "%s", name->name.c_str());
}

bool containsEntity(const std::vector<EntityId>& ids, EntityId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void selectEntityRange(SceneDocument& document,
        const std::vector<EntityRecord>& records,
        EntityId anchor,
        EntityId clicked) {
    document.clearSelection();
    if (anchor == InvalidEntityId || anchor == clicked) {
        document.setSelected(clicked, true);
        document.setActiveSelection(clicked);
        return;
    }

    bool selecting = false;
    for (const EntityRecord& record : records) {
        const bool endpoint = record.id == anchor || record.id == clicked;
        if (endpoint && !selecting) {
            selecting = true;
            document.setSelected(record.id, true);
            continue;
        }

        if (selecting) {
            document.setSelected(record.id, true);
        }

        if (endpoint) {
            document.setActiveSelection(clicked);
            return;
        }
    }

    document.clearSelection();
    document.setSelected(clicked, true);
    document.setActiveSelection(clicked);
}

Vec3 componentScaleRatio(Vec3 next, Vec3 previous) {
    auto ratio = [](float nextValue, float previousValue) {
        const float denominator = std::abs(previousValue) <= 0.000001f ? 1.0f : previousValue;
        return nextValue / denominator;
    };
    return {ratio(next.x, previous.x), ratio(next.y, previous.y), ratio(next.z, previous.z)};
}

Vec3 multiplyComponents(Vec3 left, Vec3 right) {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

Vec3 clampScale(Vec3 scale) {
    return {
            std::max(scale.x, 0.001f),
            std::max(scale.y, 0.001f),
            std::max(scale.z, 0.001f),
    };
}

const char* selectionModeLabel(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return "Vertex";
    case ModelingSelectionMode::Edge:
        return "Edge";
    case ModelingSelectionMode::Face:
        return "Face";
    case ModelingSelectionMode::Object:
        return "Object";
    }
    return "Object";
}

const char* activeSelectionKindLabel(ModelingActiveSelectionKind kind) {
    switch (kind) {
    case ModelingActiveSelectionKind::None:
        return "None";
    case ModelingActiveSelectionKind::Object:
        return "Object";
    case ModelingActiveSelectionKind::Vertex:
        return "Vertex";
    case ModelingActiveSelectionKind::Edge:
        return "Edge";
    case ModelingActiveSelectionKind::Face:
        return "Face";
    case ModelingActiveSelectionKind::Light:
        return "Light";
    }
    return "None";
}

void loadDefaultFont(const std::filesystem::path& executableDir) {
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path fontPath = executableDir / "fonts" / "Inter.ttf";
    ImFontConfig config;
    config.FontDataOwnedByAtlas = true;
    std::snprintf(config.Name, sizeof(config.Name), "Inter");

    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 18.0f, &config);
    if (font == nullptr) {
        LOG_WARN("Unable to load Inter font from {}; using ImGui default font", fontPath.string());
        io.Fonts->AddFontDefault();
        return;
    }

    io.FontDefault = font;
    LOG_INFO("Loaded Inter as default ImGui font from {}", fontPath.string());
}

} // namespace

UiSystem::~UiSystem() {
    shutdown();
}

void UiSystem::initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir) {
    window_ = window;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "quader_ui.ini";
    loadDefaultFont(executableDir);

    applyTheme();

    LOG_INFO("Initializing ImGui SDL3 backend");
    ImGui_ImplSDL3_InitForOther(window_);
    LOG_INFO("Initializing ImGui Filament renderer");
    renderer_.initialize(renderSystem.engine(), renderSystem.imguiMaterial(), renderSystem.textures());
    initialized_ = true;
    LOG_INFO("ImGui docking UI initialized");
}

void UiSystem::shutdown() {
    if (!initialized_) {
        return;
    }

    renderer_.shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
    window_ = nullptr;
}

bool UiSystem::processEvent(const SDL_Event& event) {
    return ImGui_ImplSDL3_ProcessEvent(&event);
}

void UiSystem::beginFrame() {
    renderer_.releaseFrameResources();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void UiSystem::draw(ViewportRenderer& viewport,
        ViewportCameraController& camera,
        SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        bool& quitRequested) {
    (void)assets;
    drawDockspace(quitRequested);
    drawOutliner(document);
    viewportPanel_.draw(viewport, camera, renderBridge, document, assets, modeling, defaultMaterial);
    drawInspector(document, modeling);
    consolePanel_.draw();
}

void UiSystem::prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    ImGui::Render();
    renderer_.prepare(ImGui::GetDrawData(), framebufferWidth, framebufferHeight);
}

void UiSystem::render(RenderSystem& renderSystem) {
    renderer_.render(renderSystem.renderer());
}

void UiSystem::drawDockspace(bool& quitRequested) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Main Dockspace", nullptr, flags);
    ImGui::PopStyleVar(2);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                quitRequested = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspaceId = ImGui::GetID("QuaderDockspace");
    const ImVec2 dockspaceSize = ImGui::GetContentRegionAvail();
    if (!dockLayoutBuilt_ && dockspaceSize.x > 1.0f && dockspaceSize.y > 1.0f) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

        ImGuiID mainId = dockspaceId;
        const ImGuiID consoleId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.24f, nullptr, &mainId);
        const ImGuiID outlinerId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.18f, nullptr, &mainId);
        const ImGuiID inspectorId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.24f, nullptr, &mainId);
        ImGui::DockBuilderDockWindow("Outliner", outlinerId);
        ImGui::DockBuilderDockWindow("Viewport", mainId);
        ImGui::DockBuilderDockWindow("Inspector", inspectorId);
        ImGui::DockBuilderDockWindow("Console", consoleId);
        ImGui::DockBuilderFinish(dockspaceId);
        dockLayoutBuilt_ = true;
    }

    ImGui::DockSpace(dockspaceId, {0.0f, 0.0f}, ImGuiDockNodeFlags_None);

    ImGui::End();
}

void UiSystem::drawOutliner(SceneDocument& document) {
    if (!ImGui::Begin("Outliner")) {
        ImGui::End();
        return;
    }

    const std::vector<EntityRecord> records = document.entityRecords();
    const std::vector<EntityId> selected = document.selectedEntities();
    const ImGuiIO& io = ImGui::GetIO();
    for (const EntityRecord& record : records) {
        ImGui::PushID(static_cast<int>(record.id));

        bool visible = record.visible;
        if (ImGui::Checkbox("##visible", &visible)) {
            document.setVisible(record.id, visible);
        }
        ImGui::SameLine();

        const char* suffix = record.hasModelingObject ? " [Mesh]" : (record.hasLight ? " [Light]" : "");
        const std::string label = record.name + suffix;
        if (ImGui::Selectable(label.c_str(), record.selected, ImGuiSelectableFlags_SpanAvailWidth)) {
            if (io.KeyShift) {
                selectEntityRange(document, records, outlinerAnchor_, record.id);
            } else if (io.KeyCtrl || io.KeySuper) {
                document.setSelected(record.id, !containsEntity(selected, record.id));
            } else {
                document.clearSelection();
                document.setSelected(record.id, true);
            }
            outlinerAnchor_ = record.id;
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void UiSystem::drawInspector(SceneDocument& document, ModelingSessionService& modeling) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    const std::vector<EntityId> selected = document.selectedEntities();
    const ModelingSelectionSummary selection = modeling.selectionSummary();
    if (selected.empty()) {
        if (selection.hasSelection) {
            ImGui::Text("Mode: %s", selectionModeLabel(selection.mode));
            ImGui::Text("Active: %s", activeSelectionKindLabel(selection.activeKind));
            ImGui::Text("Vertices: %u", selection.selectedVertexCount);
            ImGui::Text("Edges: %u", selection.selectedEdgeCount);
            ImGui::Text("Faces: %u", selection.selectedFaceCount);
        } else {
            ImGui::TextUnformatted("No selection");
        }
        ImGui::End();
        return;
    }

    EntityId primary = document.primarySelection();
    if (primary == InvalidEntityId) {
        primary = selected.front();
    }
    ImGui::Text("%llu selected", static_cast<unsigned long long>(selected.size()));
    ImGui::Text("Mode: %s", selectionModeLabel(selection.mode));
    if (selection.mode != ModelingSelectionMode::Object) {
        ImGui::Text("Active: %s", activeSelectionKindLabel(selection.activeKind));
        if (selection.selectedVertexCount > 0u) {
            ImGui::Text("Vertices: %u", selection.selectedVertexCount);
        }
        if (selection.selectedEdgeCount > 0u) {
            ImGui::Text("Edges: %u", selection.selectedEdgeCount);
        }
        if (selection.selectedFaceCount > 0u) {
            ImGui::Text("Faces: %u", selection.selectedFaceCount);
        }
    }

    if (selected.size() == 1u) {
        std::array<char, 128> nameBuffer;
        copyNameToBuffer(document.name(primary), nameBuffer);
        if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            document.renameEntity(primary, nameBuffer.data());
        }
    }

    bool hasVisibility = false;
    bool allVisible = true;
    for (const EntityId id : selected) {
        if (const VisibilityComponent* visibility = document.visibility(id)) {
            hasVisibility = true;
            allVisible = allVisible && visibility->visible;
        }
    }
    if (hasVisibility) {
        bool visible = allVisible;
        if (ImGui::Checkbox("Visible", &visible)) {
            for (const EntityId id : selected) {
                document.setVisible(id, visible);
            }
        }
    }

    TransformComponent* transform = document.transform(primary);
    if (transform) {
        std::size_t transformCount = 0;
        for (const EntityId id : selected) {
            if (document.transform(id)) {
                ++transformCount;
            }
        }

        ImGui::SeparatorText("Transform");
        if (selected.size() > 1u) {
            ImGui::TextDisabled("%llu spatial entities", static_cast<unsigned long long>(transformCount));
        }

        auto applyToSelectedTransforms = [&](auto mutate) {
            for (const EntityId id : selected) {
                if (TransformComponent* current = document.transform(id)) {
                    TransformComponent next = *current;
                    mutate(next);
                    document.setTransform(id, next);
                }
            }
            transform = document.transform(primary);
        };

        float position[3] = {transform->position.x, transform->position.y, transform->position.z};
        if (ImGui::DragFloat3("Position", position, 0.05f)) {
            const Vec3 previous = transform->position;
            const Vec3 nextPosition{position[0], position[1], position[2]};
            const Vec3 delta = nextPosition - previous;
            applyToSelectedTransforms([&](TransformComponent& next) {
                next.position = next.position + delta;
            });
        }

        float rotation[3] = {
                transform->rotationEuler.x * RadToDeg,
                transform->rotationEuler.y * RadToDeg,
                transform->rotationEuler.z * RadToDeg,
        };
        if (ImGui::DragFloat3("Rotation", rotation, 0.5f)) {
            const Vec3 previous = transform->rotationEuler;
            const Vec3 nextRotation{rotation[0] * DegToRad, rotation[1] * DegToRad, rotation[2] * DegToRad};
            const Vec3 delta = nextRotation - previous;
            applyToSelectedTransforms([&](TransformComponent& next) {
                next.rotationEuler = next.rotationEuler + delta;
            });
        }

        float scale[3] = {transform->scale.x, transform->scale.y, transform->scale.z};
        if (ImGui::DragFloat3("Scale", scale, 0.02f, 0.001f, 1000.0f)) {
            const Vec3 ratio = componentScaleRatio({scale[0], scale[1], scale[2]}, transform->scale);
            applyToSelectedTransforms([&](TransformComponent& next) {
                next.scale = clampScale(multiplyComponents(next.scale, ratio));
            });
        }
    }

    if (selected.size() > 1u) {
        std::size_t meshCount = 0;
        std::size_t lightCount = 0;
        std::size_t modelingCount = 0;
        for (const EntityId id : selected) {
            meshCount += document.mesh(id) ? 1u : 0u;
            lightCount += document.light(id) ? 1u : 0u;
            modelingCount += document.modelingObject(id) ? 1u : 0u;
        }

        ImGui::SeparatorText("Components");
        ImGui::Text("Meshes: %llu", static_cast<unsigned long long>(meshCount));
        ImGui::Text("Lights: %llu", static_cast<unsigned long long>(lightCount));
        ImGui::Text("SDK Objects: %llu", static_cast<unsigned long long>(modelingCount));
        ImGui::End();
        return;
    }

    if (const ModelingObjectComponent* object = document.modelingObject(primary)) {
        ImGui::SeparatorText("Modeling");
        ImGui::Text("SDK Object: %d", object->objectId);
        ImGui::Text("Material Slot: %u", object->materialSlot);
    }

    if (const MeshComponent* mesh = document.mesh(primary)) {
        ImGui::SeparatorText("Assets");
        ImGui::Text("Mesh Asset: %llu", static_cast<unsigned long long>(mesh->mesh));
        if (const MaterialComponent* material = document.material(primary)) {
            ImGui::Text("Material Asset: %llu", static_cast<unsigned long long>(material->material));
        }
    }

    ImGui::End();
}

void UiSystem::applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
}

} // namespace quader
