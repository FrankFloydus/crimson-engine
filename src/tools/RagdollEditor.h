#pragma once
#include "editor/RagdollDocument.h"
#include "editor/EditorResources.h"
#include "physics/PhysicsWorld.h"
#include "RagdollBodyGizmo.h"
#include <array>
#include <span>

namespace crimson {
class AssetLibrary;

class RagdollEditor {
public:
    explicit RagdollEditor(const std::filesystem::path& root):document_(root){}
    bool focused() const;
    bool isOpen() const {return document_.isOpen();}
    bool transactionActive() const {return document_.transactionActive();}
    bool canUndo() const {return document_.canUndo();}
    bool canRedo() const {return document_.canRedo();}
    bool canSave() const {return document_.editable();}
    bool canSaveAs() const {return isOpen();}
    void undo(){finishInteraction();resetSimulation();document_.undo();}
    void redo(){finishInteraction();resetSimulation();document_.redo();}
    void finishInteraction(bool preserveActiveItem=false);
    void cancelInteraction();
    void requestOpen(std::string id,AssetLibrary&);
    void requestExit();
    bool takeExitRequest(){const auto value=exitReady_;exitReady_=false;return value;}
    void save(AssetLibrary&,bool saveAs=false);
    void draw(AssetLibrary&,Renderer&,ResourceManager&,EditorResources&,double dt);
    void filesChanged(std::span<const std::filesystem::path>);
    void setAntiAliasing(AntiAliasing value){settings_.antiAliasing=value;}
private:
    enum class Pending {None,Open,Close,Exit};
    RagdollDocument document_;
    Pending pending_{Pending::None};
    std::string pendingId_,message_,publishedId_,previewStatus_,rebindModel_;
    std::array<char,256> saveName_{};
    std::uint64_t publishedRevision_{},environmentRevision_{},simulationRevision_{};
    bool dirtyPopup_{},savePopup_{},externalPopup_{},externalChanged_{},focusNext_{},exitReady_{},continueAfterSave_{},suppressFinalUpdate_{};
    bool simulating_{},paused_{},framePending_{true},dragging_{},showBones_{true},showShapes_{true},showJoints_{true};
    bool proportionLock_{};
    int gizmoOperation_{},simulationMode_{};
    RagdollBodyGizmo gizmo_;
    std::uint64_t testRevision_{};
    std::vector<std::uint8_t> manualPins_,scopePins_;
    float dropHeight_{.5f};
    double seconds_{},accumulator_{};
    int previewGesture_{},propertySection_{},selectedBody_{-1},selectedNode_{-1},startingClip_{-1},rebindSkin_{};
    Vec3 dragPoint_{},dragNormal_{};
    Mat4 previewWorld_{};
    math::AffineTransformFields bodyRotationFields_{},jointRotationFields_{};
    int rotationBody_{-1};
    std::uint16_t displayedTarget_{UINT16_MAX};
    SceneSettings settings_;
    ResourceHandle<PreparedEnvironment> environment_;
    std::shared_ptr<const ModelAsset> boundSource_;
    std::unique_ptr<PhysicsWorld> simulation_;
    Scene floor_;
    std::vector<std::uint32_t> selectedNodes_,mappedNodes_;
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    void modals(AssetLibrary&);
    void properties(AssetLibrary&,const std::shared_ptr<const ModelAsset>&,bool canceled);
    void sourceProperties(AssetLibrary&,const std::shared_ptr<const ModelAsset>&);
    void preview(AssetLibrary&,Renderer&,ResourceManager&,EditorResources&,double dt);
    void overlays(const ModelAsset&,const ModelPose&,const ViewportRegion&);
    void resetSimulation();
    void endDrag();
    void startSimulation(const std::shared_ptr<const ModelAsset>&,const ModelPose&);
    void testControls();
};
}
