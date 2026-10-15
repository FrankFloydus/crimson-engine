#include "ParticleEditor.h"
#include "MaterialEditor.h"
#include "DocumentEditorMenu.h"
#include "ParticleEmissionEditor.h"
#include "Tools.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <numbers>
#include <utility>

namespace crimson {
namespace {
// Derive the simple controls without publishing a migration merely because an
// older effect was opened. The result becomes authored only on an actual edit.
ParticleEmitterDefinition editableLaunch(const ParticleEmitterDefinition& source) {
    auto result=source;
    if(source.velocityMode==ParticleVelocityMode::Vector) {
        result.direction=math::normalized(source.initialVelocity,math::normalized(source.direction));
        result.speed=static_cast<float>(math::length(source.initialVelocity));
        result.speedRange.enabled=false;
        result.velocityMode=ParticleVelocityMode::DirectionSpeed;
        result.directionMode=ParticleDirectionMode::RandomCone;
        result.directionSpread=0;result.directionFlatness=0;
    } else if(source.directionMode==ParticleDirectionMode::Fixed) {
        result.directionMode=ParticleDirectionMode::RandomCone;
        result.directionSpread=0;
    }
    return result;
}
bool overallColorMultiplierActive(const ParticleEmitterDefinition& emitter) {
    const auto& track=emitter.intensityTrack;
    if(track.mode==ParticleAppearanceMode::RandomRange && track.range)
        return track.range->minimum!=1 || track.range->maximum!=1;
    if(track.mode==ParticleAppearanceMode::Curve)
        return std::any_of(track.curve.keys.begin(),track.curve.keys.end(),[](const auto& key){return key.value!=1;});
    return emitter.intensity!=1;
}
}
bool ParticleEditor::isOpen() const { return document_.isOpen(); }
bool ParticleEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen() && window && window->RootWindow->ID==ImHashStr("###ParticleEffect");
}
bool ParticleEditor::canUndo() const { return document_.canUndo(); }
bool ParticleEditor::canRedo() const { return document_.canRedo(); }
void ParticleEditor::undo() { document_.undo(); }
void ParticleEditor::redo() { document_.redo(); }
void ParticleEditor::finishInteraction() {
    const bool editing=document_.transactionActive();
    if(editing && !document_.commit())return;
    appearanceEditOwner_=0;
    if(editing) {
        // Gradient dragging also retains state outside ImGui's active ID.
        // Menus end that gesture before Save/Undo can change the document.
        for(auto& editor:curveEditors_)editor.cancel();
        gradientEditor_.cancel();colorEditor_.cancel();
        const auto* active=ImGui::GetCurrentContext()->ActiveIdWindow;
        for(auto* window=active;window;window=window->ParentWindow)
            if(window->RootWindow->ID==ImHashStr("###ParticleEffect")){ImGui::ClearActiveID();break;}
        suppressCanceledUpdate_=true;
    }
}
void ParticleEditor::cancelInteraction() {
    const bool canceled=document_.transactionActive();
    document_.cancel();previewGesture_=0;appearanceEditOwner_=0;
    for(auto& editor:curveEditors_)editor.cancel();
    gradientEditor_.cancel();colorEditor_.cancel();
    const auto* context=ImGui::GetCurrentContext();
    const auto* active=context?context->ActiveIdWindow:nullptr;
    bool owned=false;
    for(auto* window=active;window;window=window->ParentWindow)
        if(window->RootWindow->ID==ImHashStr("###ParticleEffect")){owned=true;break;}
    if(owned)ImGui::ClearActiveID();
    // InputText can return its final buffer when deactivated. Ignore that
    // delivery after cancellation so it cannot create another undo command.
    suppressCanceledUpdate_|=canceled || owned;
}
void ParticleEditor::framePreview() {
    preview_.camera=Camera{};
    const auto& emitter=document_.definition().emitter;
    Vec3 extent{};
    if(emitter.shape==ParticleShape::Sphere)extent={emitter.radius,emitter.radius,emitter.radius};
    if(emitter.shape==ParticleShape::Cone)extent={emitter.radius,0,emitter.radius};
    if(emitter.shape==ParticleShape::Box)extent=emitter.boxSize*.5;
    Vec3 minimum=extent*-1,maximum=extent;
    const double lifetime=std::min(10.f,emitter.lifetimeRange.enabled?emitter.lifetimeRange.maximum:emitter.lifetime);
    Vec3 velocityMinimum=emitter.initialVelocity,velocityMaximum=emitter.initialVelocity;
    if(emitter.velocityMode==ParticleVelocityMode::DirectionSpeed) {
        const float low=emitter.speedRange.enabled?emitter.speedRange.minimum:emitter.speed;
        const float high=emitter.speedRange.enabled?emitter.speedRange.maximum:emitter.speed;
        if(emitter.directionMode==ParticleDirectionMode::Fixed) {
            const auto direction=math::normalized(emitter.direction);
            for(std::size_t axis=0;axis<3;++axis) {
                velocityMinimum[axis]=std::min(direction[axis]*low,direction[axis]*high);
                velocityMaximum[axis]=std::max(direction[axis]*low,direction[axis]*high);
            }
        } else if(emitter.directionMode==ParticleDirectionMode::RandomCone) {
            const auto direction=math::normalized(emitter.direction);
            const double spread=emitter.directionSpread*std::numbers::pi/180;
            for(std::size_t axis=0;axis<3;++axis) {
                const double angle=std::acos(std::clamp(double(direction[axis]),-1.0,1.0));
                const double directionMaximum=angle<=spread?1:std::cos(angle-spread);
                const double directionMinimum=std::numbers::pi-angle<=spread?-1:std::cos(angle+spread);
                velocityMinimum[axis]=static_cast<float>(std::min(directionMinimum*low,directionMinimum*high));
                velocityMaximum[axis]=static_cast<float>(std::max(directionMaximum*low,directionMaximum*high));
            }
        } else { velocityMinimum={-high,-high,-high};velocityMaximum={high,high,high}; }
    }
    Vec3 force{};
    if(emitter.gravityBehavior.present && emitter.gravityBehavior.enabled)force=force+emitter.gravity;
    if(emitter.accelerationBehavior.present && emitter.accelerationBehavior.enabled)force=force+emitter.acceleration;
    for(std::size_t axis=0;axis<3;++axis) {
        const double acceleration=force[axis];
        for(const double velocity:{double(velocityMinimum[axis]),double(velocityMaximum[axis])}) {
            const auto includeTime=[&](double time) {
                const auto position=static_cast<float>(velocity*time+.5*acceleration*time*time);
                minimum[axis]=std::min(minimum[axis],position-extent[axis]);
                maximum[axis]=std::max(maximum[axis],position+extent[axis]);
            };
            includeTime(lifetime);
            if(acceleration!=0) { const auto turn=-velocity/acceleration;if(turn>0 && turn<lifetime)includeTime(turn); }
        }
    }
    float size=emitter.size;
    if(emitter.sizeTrack.mode==ParticleAppearanceMode::RandomRange && emitter.sizeTrack.range)size=emitter.sizeTrack.range->maximum;
    if(emitter.sizeTrack.mode==ParticleAppearanceMode::Curve) {
        size=0;for(const auto& key:emitter.sizeTrack.curve.keys)size=std::max(size,key.value);
    }
    const float padding=std::max(.5f,size);
    preview_.camera.frameBounds(minimum-Vec3{padding,padding,padding},maximum+Vec3{padding,padding,padding},1.4f);
    preview_.exposure=1;
}
void ParticleEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    pending_=pending;pendingId_=std::move(id);
    if(document_.isOpen() && document_.dirty()) dirtyPopup_=true;
    else if(assets) perform(*assets);
    else if(pending==Pending::Exit) { pending_=Pending::None;exitReady_=true; }
}
void ParticleEditor::requestExit() { if(!exitReady_) request(Pending::Exit,{},nullptr); }
void ParticleEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(document_.isOpen() && document_.assetId()==id) { focusNext_=true;return; }
    request(Pending::Open,std::move(id),&assets);
}
void ParticleEditor::requestCreate(std::string folder,AssetLibrary& assets) {
    pendingFolder_=std::move(folder);request(Pending::Create,{},&assets);
}
void ParticleEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;
    pending_=Pending::None;
    continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        document_.close();simulation_.stop();assets.protectParticleDocument({});assets.setParticleDocumentReferences({},{});assets.refreshParticles();
        return;
    }
    message_.clear();
    bool loaded=false;
    if(pending==Pending::Open) loaded=document_.load(pendingId_);
    if(pending==Pending::Create) {
        loaded=document_.create();
        if(loaded) {
            assets.noteAssetWrite(document_.assetId());assets.refreshParticles();
            try {
                if(!pendingFolder_.empty()) {
                    const std::array<std::string,1> ids{document_.assetId()};
                    assets.browserCatalog().move(ids,pendingFolder_);assets.noteAssetWrite("assets/browser.json");
                }
                assets.select(document_.assetId());
            } catch(const std::exception& error) { message_=error.what(); }
        }
    }
    if(loaded) {
        curveEditors_={};gradientEditor_={};colorEditor_={};appearanceEditOwner_=0;overallColorEditOwner_=0;
        assets.protectParticleDocument(document_.assetId());publishedRevision_=0;previewRevision_=0;
        publish(assets);assets.refreshParticles();framePreview();focusNext_=true;externalChanged_=false;materialDiagnostic_.clear();
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
void ParticleEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen()) return;
    assets.setParticleDocumentReferences({document_.definition().emitter.material.id},document_.historicalMaterialReferences());
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        assets.publishParticle(document_.committed());publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void ParticleEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    publish(assets);
    if(saveAs) {
        std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;
    }
    if(document_.externalFileChanged()) { externalPopup_=true;message_="The effect changed on disk. Choose Overwrite, Reload, or Save As.";return; }
    if(document_.save()) { assets.noteAssetWrite(document_.assetId());assets.refreshParticles();message_.clear(); }
    else message_=document_.status();
}
void ParticleEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!document_.isOpen()) return;
    for(const auto& path:paths) if(path.filename()==pathFromUtf8(document_.assetId()).filename())
        externalChanged_=true;
}
void ParticleEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_) { ImGui::OpenPopup("Unsaved particle effect");dirtyPopup_=false; }
    if(ImGui::BeginPopupModal("Unsaved particle effect",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save changes to %s?",document_.definition().name.c_str());
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;
            save(assets);
            if(!document_.dirty()) { perform(assets);continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
            else if(externalPopup_ || savePopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Discard")) {
            if(document_.discard()) { publish(assets);perform(assets);ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(savePopup_) { ImGui::OpenPopup("Save particle effect as");savePopup_=false; }
    if(ImGui::BeginPopupModal("Save particle effect as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save") || enter) && saveName_[0]) {
            const auto previous=document_.assetId();
            if(document_.save(saveName_.data(),true)) {
                assets.protectParticleDocument(document_.assetId());
                try { assets.publishParticle(std::make_shared<ParticleEffectAsset>(loadParticleAsset(assets.projectRoot(),previous))); }
                catch(const std::exception& error) { message_=error.what(); }
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshParticles();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;
                ImGui::CloseCurrentPopup();
            } else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) { if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_) { ImGui::OpenPopup("Particle effect changed on disk");externalPopup_=false; }
    if(ImGui::BeginPopupModal("Particle effect changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The effect changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshParticles();message_.clear();
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();
        if(ImGui::Button("Reload and discard edits")) {
            const auto id=document_.assetId();
            if(document_.load(id)) {
                publish(assets);message_.clear();if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();if(ImGui::Button("Save As")) { save(assets,true);ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
}
void ParticleEditor::materialControls(AssetLibrary& assets,MaterialEditor& materials) {
    const auto materialId=assets.references().resolveParticleMaterial(document_.definition().emitter.material.id);
    const auto material=assets.findMaterial(materialId);
    const auto assign=[&](const std::string& id) {
        const auto candidate=assets.findMaterial(id);
        if(!candidate){message_="Choose a material asset.";return;}
        if(document_.transactionActive() && !document_.commit())return;
        document_.edit("Assign particle material",[&](auto& asset){asset.emitter.material.id=id;});message_.clear();
    };
    ImGui::BeginGroup();
    const float height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float width=std::max(height,ImGui::GetContentRegionAvail().x-height*2-gap*2);
    bool picker=ImGui::InvisibleButton("Particle material",{width,height});
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    float textX=minimum.x+3;
    if(material) {
        const auto imageId=assets.references().resolveParticleTexture(material->surface.baseColorTexture.image.id);const auto* image=assets.find(imageId);
        if(image && bgfx::isValid(image->thumbnail)){draw->AddImage(ImGuiBgfx::textureId(image->thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});textX+=height;}
    }
    const auto label=material?assets.displayName(materialId):"Missing: "+assets.displayName(document_.definition().emitter.material.id);
    draw->PushClipRect({textX,minimum.y},{maximum.x-3,maximum.y},true);
    draw->AddText({textX,minimum.y+(height-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),label.c_str());draw->PopClipRect();
    ImGui::SameLine(0,gap);picker|=ImGui::Button("...##material",{height,height});
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(document_.definition().emitter.material.id==DefaultParticleMaterialId);
    if(ImGui::Button("x##material",{height,height}))assign(DefaultParticleMaterialId);
    ImGui::EndDisabled();ImGui::EndGroup();
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_MATERIAL_ASSET")) {
            const auto* bytes=static_cast<const char*>(payload->Data);
            if(payload->DataSize>1 && bytes && bytes[payload->DataSize-1]=='\0' && std::find(bytes,bytes+payload->DataSize-1,'\0')==bytes+payload->DataSize-1)
                assign(std::string(bytes,static_cast<size_t>(payload->DataSize-1)));
            else message_="The material payload is invalid.";
        }
        ImGui::EndDragDropTarget();
    }
    static ImGuiTextFilter filter;
    if(picker){filter.Clear();ImGui::OpenPopup("Select material");}
    if(ImGui::BeginPopup("Select material")) {
        filter.Draw("Search");ImGui::BeginChild("Material choices",{320,ImGui::GetFontSize()*12});
        for(const auto& entry:assets.catalog())if(entry.kind==AssetKind::Material && filter.PassFilter(assets.displayName(entry.id).c_str())) {
            const auto candidate=assets.findMaterial(entry.id);if(!candidate)continue;
            ImGui::PushID(entry.id.c_str());
            if(ImGui::Selectable(assets.displayName(entry.id).c_str(),entry.id==materialId)){assign(entry.id);ImGui::CloseCurrentPopup();}
            ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndPopup();
    }
    ImGui::BeginDisabled(!material);
    if(ImGui::SmallButton("Edit Material"))materials.requestOpen(materialId,assets);
    ImGui::EndDisabled();
}
void ParticleEditor::authoringControls(AssetLibrary& assets,MaterialEditor& materials,bool canceled) {
    const auto assetControl=[&](const char* label,const auto& draw) {
        auto candidate=document_.definition();
        const bool changed=draw(candidate);
        if(!canceled && (ImGui::IsItemActivated() || changed) && !document_.transactionActive())document_.begin(label);
        if(!canceled && changed)document_.update([&](auto& asset){asset=candidate;});
        if(ImGui::IsItemDeactivated() && document_.transactionActive())document_.commit();
    };
    const auto control=[&](const char* label,const auto& draw) {
        assetControl(label,[&](auto& asset){return draw(asset.emitter);});
    };
    const auto edit=[&](const char* label,const auto& mutation) {
        if(canceled || (document_.transactionActive() && !document_.commit()))return;
        document_.edit(label,mutation);
    };
    const auto tooltip=[](const char* text) {
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",text);
    };
    const auto launchControl=[&](const char* label,const auto& draw) {
        control(label,[&](auto& emitter) {
            auto displayed=editableLaunch(emitter);
            const bool changed=draw(displayed);
            if(changed)emitter=std::move(displayed);
            return changed;
        });
    };
    const auto range=[&](const char* title,ParticleRandomRange ParticleEmitterDefinition::* field,float minimum,float maximum,bool launch=false) {
        ImGui::PushID(title);
        control("Change random range",[&](auto& emitter) {
            auto displayed=launch?editableLaunch(emitter):emitter;
            auto& values=displayed.*field;float pair[2]{values.minimum,values.maximum};
            const bool changed=ImGui::DragFloat2(title,pair,.01f,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            if(changed) {
                // Moving one end beyond the other keeps the range usable while dragging.
                if(pair[0]>pair[1]) {
                    if(pair[0]!=values.minimum)pair[1]=pair[0];else pair[0]=pair[1];
                }
                values.minimum=pair[0];values.maximum=pair[1];
                if(launch)emitter=std::move(displayed);else emitter.*field=values;
            }
            return changed;
        });
        tooltip("Random value between minimum and maximum.");
        ImGui::PopID();
    };
    ImGui::TextUnformatted(document_.definition().emitter.name.c_str());
    const auto subsectionFlags=ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth;
    if(ImGui::CollapsingHeader("Emitter Update",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::TreeNodeEx("Timing & emission",subsectionFlags)) {
            control("Enable emitter",[](auto& emitter){return ImGui::Checkbox("Enabled",&emitter.enabled);});
            tooltip("Disabling stops emission; live particles finish.");
            control("Change emitter timing",[&](auto& emitter) {
                int mode=emitter.finiteDuration?(emitter.loop?2:1):0;
                const bool changed=ImGui::Combo("Playback",&mode,"Continuous\0One shot\0Looping\0");
                if(changed) {
                    if(mode!=0 && particleDurationLowerBound(emitter)>MaximumParticleTime) {
                        message_="A retained legacy emission event prevents a finite cycle within the supported duration.";return false;
                    }
                    emitter.finiteDuration=mode!=0;emitter.loop=mode==2;
                    if(emitter.finiteDuration)emitter.duration=std::max(emitter.duration,particleDurationLowerBound(emitter));
                    message_.clear();
                }
                return changed;
            });
            control("Change emitter delay",[](auto& emitter){return ImGui::DragFloat("Delay (s)",&emitter.delay,.01f,0,MaximumParticleTime,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            tooltip("Delay before the first emission cycle.");
            control("Change emission rate",[](auto& emitter){return ImGui::DragFloat("Rate (particles/s)",&emitter.rate,.5f,0,MaximumParticleRate,"%.1f",ImGuiSliderFlags_AlwaysClamp);});
            control("Change emission explosiveness",[&](auto& emitter){return drawParticleExplosiveness(emitter,message_);});
            tooltip("0: steady emission. 1: emit the cycle's particles at its start. Count remains Rate x Duration.");
            if(document_.definition().emitter.finiteDuration || document_.definition().emitter.explosiveness>0) {
                control("Change emitter duration",[](auto& emitter){return drawParticleDuration(emitter);});
                tooltip(document_.definition().emitter.bursts.empty()?"Duration of one emission cycle.":"Duration must end after the retained legacy emission events.");
            }
            control("Change particle capacity",[](auto& emitter) {
                int capacity=static_cast<int>(emitter.capacity);
                const bool changed=ImGui::DragInt("Capacity",&capacity,1,1,static_cast<int>(MaximumParticleCapacity),"%d",ImGuiSliderFlags_AlwaysClamp);
                if(changed)emitter.capacity=static_cast<uint32_t>(capacity);return changed;
            });
            tooltip("Maximum live particles. Changes clear the live pool.");
            ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("Simulation",subsectionFlags)) {
            control("Change simulation space",[](auto& emitter) {
                int mode=static_cast<int>(emitter.space);
                const bool changed=ImGui::Combo("Simulation space",&mode,"World\0Local\0");
                if(changed)emitter.space=static_cast<ParticleSpace>(mode);return changed;
            });
            tooltip("Local particles follow the emitter. World particles detach.");
            assetControl("Change particle pre-process",[](auto& asset) {
                return ImGui::DragFloat("Pre-process (s)",&asset.preprocessSeconds,.05f,0,MaximumParticlePreprocessSeconds,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            });
            tooltip("Simulation time elapsed before the first frame.");
            assetControl("Change particle seed",[](auto& asset){return ImGui::InputScalar("Seed",ImGuiDataType_U64,&asset.defaultSeed);});
            tooltip("Seed for repeatable variation.");
            ImGui::TreePop();
        }
    }
    if(ImGui::CollapsingHeader("Spawn",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::TreeNodeEx("Shape",subsectionFlags)) {
            control("Change emission shape",[](auto& emitter) {
                int shape=static_cast<int>(emitter.shape);
                const bool changed=ImGui::Combo("Shape",&shape,"Point\0Cone\0Sphere\0Box\0");
                if(changed) {
                    emitter.shape=static_cast<ParticleShape>(shape);
                    if(emitter.shape==ParticleShape::Cone || emitter.shape==ParticleShape::Sphere) {
                        if(emitter.velocityMode==ParticleVelocityMode::Vector) {
                            const auto speed=static_cast<float>(math::length(emitter.initialVelocity));
                            emitter.direction=math::normalized(emitter.initialVelocity,math::normalized(emitter.direction));
                            if(speed>0)emitter.speed=speed;
                            emitter.speedRange.enabled=false;emitter.directionFlatness=0;
                        }
                        emitter.velocityMode=ParticleVelocityMode::DirectionSpeed;
                        emitter.directionMode=emitter.shape==ParticleShape::Cone?ParticleDirectionMode::RandomCone:ParticleDirectionMode::Outward;
                        emitter.directionSpread=emitter.shape==ParticleShape::Cone?20.f:0.f;
                    }
                }
                return changed;
            });
            const auto shape=document_.definition().emitter.shape;
            if(shape==ParticleShape::Cone || shape==ParticleShape::Sphere)
                control("Change emission radius",[](auto& emitter){return ImGui::DragFloat("Radius (m)",&emitter.radius,.01f,0,MaximumParticleShapeSize,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            if(shape==ParticleShape::Box)
                control("Change emission box",[](auto& emitter){return ImGui::DragFloat3("Dimensions XYZ (m)",emitter.boxSize.data(),.01f,0,MaximumParticleShapeSize,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("Initial velocity",subsectionFlags)) {
            launchControl("Change launch direction",[](auto& emitter) {
                const auto before=emitter.direction;
                const bool changed=ImGui::DragFloat3("Direction XYZ",emitter.direction.data(),.01f,-MaximumParticleForce,MaximumParticleForce,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                if(changed && math::length(emitter.direction)<1e-6)emitter.direction=before;
                return changed;
            });
            tooltip("Launch direction in emitter coordinates.");
            launchControl("Change speed variation",[](auto& emitter) {
                int mode=emitter.speedRange.enabled?1:0;
                const bool changed=ImGui::Combo("Speed mode",&mode,"Constant\0Random range\0");
                if(changed)emitter.speedRange.enabled=mode!=0;return changed;
            });
            if(editableLaunch(document_.definition().emitter).speedRange.enabled)
                range("Speed min/max (m/s)",&ParticleEmitterDefinition::speedRange,0,MaximumParticleSpeed,true);
            else launchControl("Change launch speed",[](auto& emitter){return ImGui::DragFloat("Speed (m/s)",&emitter.speed,.05f,0,MaximumParticleSpeed,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            launchControl("Change direction spread",[](auto& emitter){return ImGui::SliderFloat("Spread (degrees)",&emitter.directionSpread,0,180,"%.1f");});
            tooltip("Half-angle around the launch direction.");
            launchControl("Change direction flatness",[](auto& emitter){return ImGui::SliderFloat("Flatness",&emitter.directionFlatness,0,1,"%.2f");});
            tooltip("0: round spread. 1: flat fan.");
            launchControl("Change outward launch",[](auto& emitter) {
                bool outward=emitter.directionMode==ParticleDirectionMode::Outward;
                const bool changed=ImGui::Checkbox("Outward from shape",&outward);
                if(changed)emitter.directionMode=outward?ParticleDirectionMode::Outward:ParticleDirectionMode::RandomCone;
                return changed;
            });
            tooltip("Launch away from the shape center.");
            if(ImGui::SmallButton("Clear speed"))edit("Clear launch speed",[](auto& asset) {
                auto launch=editableLaunch(asset.emitter);launch.speed=0;launch.speedRange.enabled=false;asset.emitter=std::move(launch);
            });
            ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("Lifetime",subsectionFlags)) {
            control("Change lifetime variation",[](auto& emitter) {
                int mode=emitter.lifetimeRange.enabled?1:0;
                const bool changed=ImGui::Combo("Mode##lifetime",&mode,"Constant\0Random range\0");
                if(changed)emitter.lifetimeRange.enabled=mode!=0;return changed;
            });
            if(document_.definition().emitter.lifetimeRange.enabled)range("Lifetime min/max (s)",&ParticleEmitterDefinition::lifetimeRange,.001f,MaximumParticleTime);
            else control("Change lifetime",[](auto& emitter){return ImGui::DragFloat("Lifetime (s)",&emitter.lifetime,.01f,.001f,MaximumParticleTime,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            ImGui::TreePop();
        }
    }
    if(ImGui::CollapsingHeader("Update",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::TreeNodeEx("Forces",subsectionFlags)) {
            const auto behavior=[&](const char* title,ParticleBehavior ParticleEmitterDefinition::* member,const auto& fields) {
                if(!(document_.definition().emitter.*member).present)return;
                ImGui::PushID(title);ImGui::Separator();
                control("Enable particle behavior",[&](auto& emitter){return ImGui::Checkbox(title,&(emitter.*member).enabled);});
                ImGui::SameLine();
                if(ImGui::SmallButton("Remove"))edit("Remove particle behavior",[&](auto& asset){(asset.emitter.*member).present=false;});
                if((document_.definition().emitter.*member).present) {
                    ImGui::BeginDisabled(!(document_.definition().emitter.*member).enabled);fields();ImGui::EndDisabled();
                }
                ImGui::PopID();
            };
            behavior("Gravity",&ParticleEmitterDefinition::gravityBehavior,[&] {
                control("Change gravity",[](auto& emitter){return ImGui::DragFloat3("Gravity XYZ (m/s²)",emitter.gravity.data(),.05f,-MaximumParticleForce,MaximumParticleForce,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            });
            behavior("Acceleration",&ParticleEmitterDefinition::accelerationBehavior,[&] {
                control("Change acceleration",[](auto& emitter){return ImGui::DragFloat3("Acceleration XYZ (m/s²)",emitter.acceleration.data(),.05f,-MaximumParticleForce,MaximumParticleForce,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            });
            behavior("Drag",&ParticleEmitterDefinition::dragBehavior,[&] {
                control("Change drag",[](auto& emitter){return ImGui::DragFloat("Drag (1/s)",&emitter.drag,.01f,0,MaximumParticleDrag,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                tooltip("Velocity damping per second.");
            });
            if(ImGui::Button("Add behavior"))ImGui::OpenPopup("Add particle behavior");
            if(ImGui::BeginPopup("Add particle behavior")) {
                const auto add=[&](const char* title,ParticleBehavior ParticleEmitterDefinition::* member) {
                    if((document_.definition().emitter.*member).present)return;
                    if(ImGui::MenuItem(title))edit("Add particle behavior",[&](auto& asset){auto& entry=asset.emitter.*member;entry.present=true;entry.enabled=true;});
                };
                add("Gravity",&ParticleEmitterDefinition::gravityBehavior);
                add("Acceleration",&ParticleEmitterDefinition::accelerationBehavior);
                add("Drag",&ParticleEmitterDefinition::dragBehavior);
                const auto& emitter=document_.definition().emitter;
                if(emitter.gravityBehavior.present && emitter.accelerationBehavior.present && emitter.dragBehavior.present)ImGui::TextDisabled("All available behaviors added");
                ImGui::EndPopup();
            }
            ImGui::TreePop();
        }
    }
    bool appearanceActive=false;
    if(ImGui::CollapsingHeader("Render",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::TreeNodeEx("Material",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth)) {
            materialControls(assets,materials);ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("Orientation",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth)) {
            control("Change particle billboard mode",[](auto& emitter) {
                int mode=static_cast<int>(emitter.billboardMode);
                const bool changed=ImGui::Combo("Billboard",&mode,"Disabled\0Enabled\0Y-Billboard\0");
                if(changed)emitter.billboardMode=static_cast<ParticleBillboardMode>(mode);
                return changed;
            });
            tooltip("Disabled uses the particle's orientation. Enabled faces the camera. Y-Billboard constrains facing around world Y.");
            ImGui::BeginDisabled(document_.definition().emitter.billboardMode!=ParticleBillboardMode::Disabled);
            control("Change particle orientation",[](auto& emitter) {
                return ImGui::DragFloat3("Orientation XYZ (degrees)",emitter.orientation.data(),.5f,-MaximumParticleRotation,MaximumParticleRotation,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            });
            tooltip("Orientation in emitter coordinates. World particles use the emitter's frame at birth; local particles follow its current frame.");
            ImGui::EndDisabled();ImGui::TreePop();
        }
        const auto appearanceControl=[&](ImGuiID owner,const char* label,const auto& draw) {
            auto candidate=document_.definition();
            const auto changes=draw(candidate.emitter);
            appearanceActive|=changes.active;
            if(!canceled && (changes.activated || changes.changed)) {
                if(document_.transactionActive() && appearanceEditOwner_!=owner && !document_.commit())return;
                if(!document_.transactionActive() && document_.begin(label))appearanceEditOwner_=owner;
            }
            if(!canceled && changes.changed && appearanceEditOwner_==owner)
                document_.update([&](auto& asset){asset.emitter=candidate.emitter;});
            if(changes.deactivated && !changes.active && appearanceEditOwner_==owner && document_.transactionActive())
                if(document_.commit())appearanceEditOwner_=0;
        };
        const auto collapsed=[&](ImGuiID owner,auto& state) {
            if(owner && appearanceEditOwner_==owner)finishInteraction();
            state.cancel();
        };
        const auto scalar=[&](const char* title,const char* valueLabel,float ParticleEmitterDefinition::* constant,
                              ParticleScalarTrack ParticleEmitterDefinition::* member,size_t stateIndex,float minimum,float maximum,float constantMinimum) {
            ImGui::PushID(title);
            const auto owner=ImGui::GetID("Appearance transaction");
            if(stateIndex==1)overallColorEditOwner_=owner;
            const auto summaryMode=(document_.definition().emitter.*member).mode;
            const char* summary=summaryMode==ParticleAppearanceMode::Curve?"Curve":summaryMode==ParticleAppearanceMode::RandomRange?"Random range":"Constant";
            if(!ImGui::TreeNodeEx("Property",ImGuiTreeNodeFlags_SpanAvailWidth,"%s (%s)",title,summary)) {
                if(stateIndex==2)tooltip("Spin within the particle's selected plane, including when billboarding is disabled.");
                collapsed(owner,curveEditors_[stateIndex]);ImGui::PopID();return;
            }
            if(stateIndex==2)tooltip("Spin within the particle's selected plane, including when billboarding is disabled.");
            const auto command=std::string("Edit particle ")+title;
            appearanceControl(owner,command.c_str(),[&](auto& emitter) {
                ParticleAppearanceEdit changes;auto& track=emitter.*member;auto& value=emitter.*constant;
                int mode=static_cast<int>(track.mode);
                const bool changed=ImGui::Combo("Mode",&mode,"Constant\0Random range\0Curve\0");changes.include(changed);
                if(changed) {
                    track.mode=static_cast<ParticleAppearanceMode>(mode);
                    if(track.mode==ParticleAppearanceMode::RandomRange && !track.range)track.range=ParticleAppearanceRange{value,value};
                    if(track.mode==ParticleAppearanceMode::Curve && track.curve.keys.empty())track.curve.keys={{1,0,value},{2,1,value}};
                    curveEditors_[stateIndex].cancel();
                }
                if(track.mode==ParticleAppearanceMode::Constant)
                    changes.include(ImGui::DragFloat(valueLabel,&value,.01f,constantMinimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp));
                if(track.mode==ParticleAppearanceMode::RandomRange && track.range) {
                    float bounds[2]{track.range->minimum,track.range->maximum};
                    const bool rangeChanged=ImGui::DragFloat2("Minimum / maximum",bounds,.01f,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);changes.include(rangeChanged);
                    if(rangeChanged) {
                        if(bounds[0]>bounds[1]) {
                            if(bounds[0]!=track.range->minimum)bounds[1]=bounds[0];else bounds[0]=bounds[1];
                        }
                        track.range=ParticleAppearanceRange{bounds[0],bounds[1]};
                    }
                }
                if(track.mode==ParticleAppearanceMode::Curve)
                    changes.merge(drawParticleCurveEditor(track.curve,curveEditors_[stateIndex],minimum,maximum,value,valueLabel));
                return changes;
            });
            ImGui::TreePop();ImGui::PopID();
        };
        ImGui::PushID("Color appearance");
        const auto colorOwner=ImGui::GetID("Appearance transaction");
        const bool colorOpen=ImGui::TreeNodeEx("Property",ImGuiTreeNodeFlags_SpanAvailWidth,"Color & opacity (%s%s)",
            document_.definition().emitter.colorMode==ParticleColorMode::Gradient?"Gradient":"Constant",
            overallColorMultiplierActive(document_.definition().emitter)?", brightness":"");
        tooltip("Color and Brightness feed Particle Color RGB. Opacity feeds A; connect A to Opacity, or multiply it into Opacity Mask.");
        if(colorOpen) {
            appearanceControl(colorOwner,"Edit particle color / opacity",[&](auto& emitter) {
                ParticleAppearanceEdit changes;
                int mode=static_cast<int>(emitter.colorMode);
                const bool changed=ImGui::Combo("Mode",&mode,"Constant\0Gradient\0");changes.include(changed);
                if(changed) {
                    emitter.colorMode=static_cast<ParticleColorMode>(mode);
                    if(emitter.colorMode==ParticleColorMode::Gradient && emitter.colorGradient.stops.empty()) {
                        const Vec4 color{emitter.color[0],emitter.color[1],emitter.color[2],emitter.opacity};
                        emitter.colorGradient.stops={{1,0,color},{2,1,color}};
                    }
                    gradientEditor_.cancel();colorEditor_.cancel();
                }
                if(emitter.colorMode==ParticleColorMode::Constant) {
                    const auto colorChanges=drawHdrColorControl("Color",emitter.color,&emitter.opacity,colorEditor_,false,MaximumParticleColor);
                    changes.merge({colorChanges.changed,colorChanges.activated,colorChanges.active,colorChanges.deactivated});
                } else changes.merge(drawParticleGradientEditor(emitter.colorGradient,gradientEditor_,
                    {emitter.color[0],emitter.color[1],emitter.color[2],emitter.opacity}));
                return changes;
            });
            scalar("Brightness","Multiplier",&ParticleEmitterDefinition::intensity,&ParticleEmitterDefinition::intensityTrack,1,0,100000,0);
            ImGui::TreePop();
        } else {
            collapsed(colorOwner,gradientEditor_);colorEditor_.cancel();
            collapsed(overallColorEditOwner_,curveEditors_[1]);
        }
        ImGui::PopID();
        scalar("Size","Size (m)",&ParticleEmitterDefinition::size,&ParticleEmitterDefinition::sizeTrack,0,0,10000,.0001f);
        scalar("Rotation","Rotation (degrees)",&ParticleEmitterDefinition::rotation,&ParticleEmitterDefinition::rotationTrack,2,-MaximumParticleRotation,MaximumParticleRotation,-MaximumParticleRotation);
    } else if(appearanceEditOwner_) {
        finishInteraction();for(auto& editor:curveEditors_)editor.cancel();gradientEditor_.cancel();colorEditor_.cancel();
    }
    if(document_.transactionActive() && !appearanceActive && !ImGui::IsAnyItemActive() && document_.commit())appearanceEditOwner_=0;
    if(!document_.transactionActive())appearanceEditOwner_=0;
    if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
    if(!materialDiagnostic_.empty())ImGui::TextWrapped("%s",materialDiagnostic_.c_str());
}
void ParticleEditor::draw(AssetLibrary& assets,Renderer& renderer,double dt,MaterialEditor& materials) {
    if(pending_==Pending::Exit && !document_.dirty()) perform(assets);
    if(document_.isOpen() && assets.references().deleted(document_.assetId())) {
        document_.close();assets.protectParticleDocument({});assets.setParticleDocumentReferences({},{});simulation_.stop();message_="The open particle asset was deleted.";
    }
    if(document_.isOpen() && externalChanged_ && !document_.transactionActive() && pending_==Pending::None) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty()) externalPopup_=true;
            else { const auto id=document_.assetId();if(!document_.load(id)) message_=document_.status(); }
        }
    }
    if(!isOpen()) { renderer.releaseParticlePreview();modals(assets);return; }
    if(focusNext_) {
        ImGui::SetNextWindowFocus();focusNext_=false;
    }
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
    ImGui::SetNextWindowSize({1100,700},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({720,420},{FLT_MAX,FLT_MAX});
    bool visible=true;
    const auto title="Particle Editor - "+document_.definition().name+(document_.dirty()?" *":"")+"###ParticleEffect";
    const bool contents=ImGui::Begin(title.c_str(),&visible,DocumentEditorWindowFlags);
    if(!visible) request(Pending::Close,{},&assets);
    if(contents && isOpen()) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&] {
            return DocumentEditorMenuState{isOpen(),isOpen(),canUndo(),canRedo(),document_.undoLabel(),document_.redoLabel()};
        },[&] {
            ImGui::SetNextItemWidth(ImGui::GetFontSize()*14);
            drawToneMappingControl(preview_.environment.toneMapping);
            drawGlowControls(preview_.environment.glow);
        });
        switch(action) {
        case DocumentEditorAction::Save:save(assets);break;
        case DocumentEditorAction::SaveAs:save(assets,true);break;
        case DocumentEditorAction::Close:request(Pending::Close,{},&assets);break;
        case DocumentEditorAction::Undo:undo();break;
        case DocumentEditorAction::Redo:redo();break;
        case DocumentEditorAction::None:break;
        }
        const bool escape=document_.transactionActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        const bool canceled=suppressCanceledUpdate_ || escape;
        if(escape)cancelInteraction();
        suppressCanceledUpdate_=false;
        if(isOpen() && ImGui::BeginTable("Particle authoring",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthFixed,340);
            ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::BeginChild("Particle properties",{0,0});
            authoringControls(assets,materials,canceled);
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("Particle preview",{0,0});
            if(previewRevision_!=document_.previewRevision()) {
                const auto& definition=document_.definition();
                const bool identityChanged=simulation_.definition().id!=definition.id || simulation_.definition().emitter.id!=definition.emitter.id;
                if(!previewRevision_ || identityChanged) simulation_.restart(definition,definition.defaultSeed);
                else simulation_.updateDefinition(definition);
                previewRevision_=document_.previewRevision();
            }
            if(ImGui::Button(simulation_.state()==ParticlePlaybackState::Paused?"Resume":simulation_.completed()?"Play again":"Play")) simulation_.play();
            ImGui::SameLine();if(ImGui::Button("Pause")) simulation_.pause();
            ImGui::SameLine();if(ImGui::Button("Restart")) simulation_.restart(document_.definition(),document_.definition().defaultSeed);
            ImGui::SameLine();if(ImGui::Button("Stop")) simulation_.stop();
            ImGui::SameLine();if(ImGui::Button("Frame")) framePreview();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("Frame emission and estimated motion.");
            simulation_.advance(dt);
            const auto& stats=simulation_.stats();
            const char* state=simulation_.state()==ParticlePlaybackState::Stopped?"Stopped":simulation_.state()==ParticlePlaybackState::Paused?"Paused":
                simulation_.completed()?"Completed":simulation_.draining()?"Finishing live particles":simulation_.elapsed()<document_.definition().emitter.delay?"Waiting for delay":
                !document_.definition().emitter.enabled?"Emission disabled":"Emitting";
            ImGui::Text("%s · %.2f s · %zu live / %u",state,simulation_.elapsed(),stats.live,document_.definition().emitter.capacity);
            if(stats.droppedSpawns || stats.droppedCatchUpTicks || stats.droppedScheduleSegments)
                ImGui::TextWrapped("Dropped: %llu births, %llu catch-up ticks, %llu scheduling segments",static_cast<unsigned long long>(stats.droppedSpawns),static_cast<unsigned long long>(stats.droppedCatchUpTicks),static_cast<unsigned long long>(stats.droppedScheduleSegments));
            if(stats.clearedCapacityParticles)
                ImGui::Text("Capacity edits cleared %llu particles",static_cast<unsigned long long>(stats.clearedCapacityParticles));
            const auto& renderStats=renderer.particlePreviewStats();
            if(renderStats.dropped || renderStats.invalid)
                ImGui::Text("Rendering: %zu omitted, %zu invalid",renderStats.dropped,renderStats.invalid);
            if(!renderer.particleStatus().empty()) ImGui::TextWrapped("%s",renderer.particleStatus().c_str());
            if(!renderer.glowStatus().empty())ImGui::TextWrapped("%s",renderer.glowStatus().c_str());
            const auto size=ImGui::GetContentRegionAvail();
            if(size.x>0 && size.y>0) {
                const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
                try { renderer.resizeParticlePreview(static_cast<uint32_t>(std::max(1.f,size.x*scale.x)),static_cast<uint32_t>(std::max(1.f,size.y*scale.y))); }
                catch(const std::exception& error) { message_=std::string("Preview preparation failed: ")+error.what(); }
                const auto texture=renderer.particlePreviewTexture();
                if(bgfx::isValid(texture))ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),size);
                else ImGui::Dummy(size);
                auto& io=ImGui::GetIO();
                const bool ownsInput=!io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && !ImGui::IsKeyPressed(ImGuiKey_Escape,false);
                if(ownsInput && ImGui::IsItemHovered()) {
                    if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) previewGesture_=io.KeyShift?2:1;
                    if(io.MouseWheel) preview_.camera.zoom(io.MouseWheel);
                }
                if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || !ownsInput) previewGesture_=0;
                if(previewGesture_==1) preview_.camera.orbit(io.MouseDelta.x,io.MouseDelta.y,preview_.camera.target);
                if(previewGesture_==2) preview_.camera.pan(io.MouseDelta.x,io.MouseDelta.y,size.x,size.y);
                presentation_.clear();simulation_.appendPresentation(presentation_);packets_.clear();packets_.reserve(presentation_.size());
                for(const auto& p:presentation_) packets_.push_back({p.position,p.size,p.color,0,p.rotation,p.billboardMode,p.right,p.up});
                const auto binding=assets.preparedParticleMaterial(document_.definition().emitter.material.id);
                if(!binding || !binding->ready)packets_.clear();
                materialDiagnostic_=binding?binding->diagnostic:"Particle material is unavailable.";
                renderer.renderParticlePreview(preview_,packets_,binding?std::span<const RenderParticleMaterial>(&binding->render,1):std::span<const RenderParticleMaterial>{},
                    MaterialEvaluationContext{simulation_.elapsed(),ImGui::GetTime()});
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else if(document_.transactionActive() || previewGesture_)cancelInteraction();
    ImGui::End();
    publish(assets);modals(assets);
}
}
