#include "native_scene_authoring.hpp"
#include "native_scene_projection.hpp"
#include "LevelSession.h"
#include "assets/project_asset_id.hpp"
#include "core/i_undoable_command.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "scene/document/scene_entity_state.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace crimson::integration {
namespace {
using NativeComponents=quader::EngineComponentsComponent;
using NativeSettings=quader::EngineSceneSettings;

std::optional<NativeComponents> component_value(const quader::SceneDocument& scene,quader::EntityId id){
    if(const auto* value=scene.engine_components(id))return *value;
    return {};
}

struct ComponentEdit final {
    std::string uuid;
    std::optional<NativeComponents> before,after;
};
struct CreatedEntity final {
    quader::SceneEntityState state;
    std::string parent_uuid;
};

class SceneComponentCommand final:public quader::IUndoableCommand {
public:
    SceneComponentCommand(quader::SceneDocument& scene,std::string label,
        std::vector<ComponentEdit> edits,std::vector<CreatedEntity> created,
        bool settings_changed,std::optional<NativeSettings> before,std::optional<NativeSettings> after,
        std::vector<std::string> selected,std::string primary)
        :scene_(scene),label_(std::move(label)),edits_(std::move(edits)),created_(std::move(created)),
        settings_changed_(settings_changed),before_(std::move(before)),after_(std::move(after)),
        selected_(std::move(selected)),primary_(std::move(primary)),
        previous_selection_(scene.selected_entities()),previous_primary_(scene.primary_selection()){}
    const std::string& label()const override{return label_;}
    quader::CommandResult redo()override{return apply(true);}
    quader::CommandResult undo()override{return apply(false);}
private:
    quader::CommandResult apply(bool forward){
        bool mutated=false;
        try{
            for(const auto& edit:edits_){
                const auto id=scene_.entity_for_uuid(edit.uuid);
                if(!scene_.exists(id)||component_value(scene_,id)!=(forward?edit.before:edit.after))
                    throw std::runtime_error("The scene component changed outside this command.");
            }
            if(forward)for(const auto& value:created_)if(scene_.entity_for_uuid(value.state.uuid)!=quader::kInvalidEntityId)
                throw std::runtime_error("The scene entity identity is already present.");
            mutated=true;
            if(forward){
                for(auto& value:created_){
                    const auto id=quader::create_scene_entity_from_state(scene_,value.state,quader::kInvalidEntityId,
                        value.state.name,quader::SceneEntityStateIdentityMode::Preserve);
                    if(id==quader::kInvalidEntityId)throw std::runtime_error("The scene entity could not be created.");
                    value.state.id=id;value.state.editor_object_id=scene_.editor_object_id(id);
                }
                for(const auto& value:created_)if(!value.parent_uuid.empty()){
                    const auto parent=scene_.entity_for_uuid(value.parent_uuid);
                    if(parent==quader::kInvalidEntityId||!scene_.set_parent(value.state.id,parent,false))
                        throw std::runtime_error("The scene hierarchy could not be created.");
                }
            }
            for(const auto& edit:edits_){
                const auto id=scene_.entity_for_uuid(edit.uuid);const auto& next=forward?edit.after:edit.before;
                if(next){if(!scene_.set_engine_components(id,*next))throw std::runtime_error("Invalid scene components.");}
                else scene_.clear_engine_components(id);
            }
            if(settings_changed_&&!scene_.set_engine_settings(forward?after_:before_))
                throw std::runtime_error("Invalid scene runtime settings.");
            if(!forward){
                std::vector<quader::EntityId> ids;for(const auto& value:created_)ids.push_back(value.state.id);
                if(!ids.empty()&&!scene_.destroy_entities(ids))throw std::runtime_error("The created entities could not be restored.");
                scene_.replace_selection(previous_selection_,previous_primary_);
            }else{
                std::vector<quader::EntityId> ids;for(const auto& uuid:selected_){const auto id=scene_.entity_for_uuid(uuid);if(id!=quader::kInvalidEntityId)ids.push_back(id);}
                scene_.replace_selection(ids,scene_.entity_for_uuid(primary_));
            }
            return quader::command_changed_result();
        }catch(const std::exception& error){
            if(forward&&mutated){
                for(const auto& edit:edits_)if(const auto id=scene_.entity_for_uuid(edit.uuid);id!=quader::kInvalidEntityId){if(edit.before)(void)scene_.set_engine_components(id,*edit.before);else scene_.clear_engine_components(id);}
                if(settings_changed_)(void)scene_.set_engine_settings(before_);
                std::vector<quader::EntityId> ids;for(const auto& value:created_){const auto id=scene_.entity_for_uuid(value.state.uuid);if(id!=quader::kInvalidEntityId)ids.push_back(id);}if(!ids.empty())(void)scene_.destroy_entities(ids);
                scene_.replace_selection(previous_selection_,previous_primary_);
            }
            return quader::command_invalid_result(error.what());
        }
    }
    quader::SceneDocument& scene_;
    std::string label_;
    std::vector<ComponentEdit> edits_;
    std::vector<CreatedEntity> created_;
    bool settings_changed_{};
    std::optional<NativeSettings> before_,after_;
    std::vector<std::string> selected_;
    std::string primary_;
    std::vector<quader::EntityId> previous_selection_;
    quader::EntityId previous_primary_{};
};
}
struct NativeSceneAuthoring::Impl {
    LevelSession& level;
    quader::ProjectSession& project;
    quader::SceneSession* session{};
    std::string scene_uuid;
    std::uint64_t selection_revision{UINT64_MAX},projection_generation{UINT64_MAX};
    std::uint64_t session_id{},binding_generation{UINT64_MAX},native_revision{UINT64_MAX},publication_serial{};
    std::function<void(quader::SceneDocument&)> retention_observer;
    std::optional<SceneSnapshot> accepted_edit;
    std::unordered_map<EntityId,std::string> accepted_identities;
    bool needs_reprojection{};
    Impl(LevelSession& value,quader::ProjectSession& owner):level(value),project(owner){}
    bool current()const{const auto* active=project.active_scene();return session&&active&&active->session.get()==session&&active->session_id.packed()==session_id&&active->scene_id.str()==scene_uuid;}
};
NativeSceneAuthoring::NativeSceneAuthoring(LevelSession& level,quader::ProjectSession& project):impl_(std::make_unique<Impl>(level,project)){
    level.document().setAuthoringCommit([this](const SceneSnapshot& snapshot,const std::string& label){return commit(snapshot,label);});
}
NativeSceneAuthoring::~NativeSceneAuthoring(){impl_->level.document().setAuthoringCommit({});}
void NativeSceneAuthoring::bind(quader::SceneSession* session,std::string scene_uuid){
    const auto* active=impl_->project.active_scene();const auto identity=active?active->session_id.packed():0;
    if(impl_->session==session&&impl_->scene_uuid==scene_uuid&&impl_->session_id==identity){synchronize();return;}
    (void)finish();impl_->level.stop();impl_->session=session;impl_->session_id=identity;impl_->scene_uuid=std::move(scene_uuid);
    impl_->accepted_edit.reset();impl_->accepted_identities.clear();impl_->needs_reprojection=false;
    impl_->native_revision=impl_->binding_generation=impl_->selection_revision=impl_->projection_generation=UINT64_MAX;synchronize();
}
quader::SceneSession* NativeSceneAuthoring::session()const{return impl_->current()?impl_->session:nullptr;}
void NativeSceneAuthoring::set_retention_observer(std::function<void(quader::SceneDocument&)> observer){impl_->retention_observer=std::move(observer);if(impl_->current()&&impl_->retention_observer)impl_->retention_observer(impl_->session->scene());}
bool NativeSceneAuthoring::finish(){if(!impl_->current()){impl_->level.document().cancel();return true;}return impl_->level.document().commit();}
void NativeSceneAuthoring::synchronize(){
    auto& state=*impl_;if(!state.current()||state.level.playing()||state.level.document().transactionActive())return;
    auto& native=state.session->scene();
    const auto binding=state.session->document_binding_generation();
    if(state.needs_reprojection||state.native_revision!=native.content_revision()||state.binding_generation!=binding){
        if(state.retention_observer)state.retention_observer(native);
        if(!state.level.publishNativeScene(native,state.scene_uuid,++state.publication_serial))return;
        state.native_revision=native.content_revision();state.binding_generation=binding;
        state.accepted_edit.reset();state.accepted_identities.clear();state.needs_reprojection=false;
    }
    if(state.selection_revision==native.selection_revision()&&state.projection_generation==state.level.document().documentGeneration())return;
    state.selection_revision=native.selection_revision();state.projection_generation=state.level.document().documentGeneration();
    const auto& projection=state.level.nativeProjection();std::vector<EntityId> selected;
    for(const auto id:native.selected_entities())if(const auto mapped=projection.entity_id(native.uuid(id)))selected.push_back(mapped);
    state.level.document().select(std::move(selected),projection.entity_id(native.uuid(native.primary_selection())));
}
bool NativeSceneAuthoring::commit(const SceneSnapshot& after,const std::string& label){
    auto& state=*impl_;if(!state.current()||state.level.playing()||state.session->history().transaction_active())return false;
    auto& native=state.session->scene();const auto& projection=state.level.nativeProjection();
    if(native.content_revision()!=state.native_revision||state.session->document_binding_generation()!=state.binding_generation)return false;
    if(state.retention_observer)state.retention_observer(native);
    const auto& before=state.accepted_edit?*state.accepted_edit:projection.snapshot();auto identities=state.accepted_identities;
    std::unordered_map<EntityId,const EntityRecord*> previous;
    for(const auto& record:before.records){if(!identities.contains(record.id))identities.emplace(record.id,projection.entity_uuid(record.id));previous.emplace(record.id,&record);}
    for(const auto& record:after.records)if(!identities.contains(record.id))identities.emplace(record.id,std::string(quader::ProjectAssetId::generate().str()));
    const auto resolve=[&](EntityId id){if(!id)return std::string{};const auto found=identities.find(id);if(found==identities.end())throw std::runtime_error("Missing scene entity reference.");return found->second;};
    std::vector<ComponentEdit> edits;std::vector<CreatedEntity> created;
    for(const auto& record:after.records){
        const auto uuid=resolve(record.id);const auto next=projection.engine_components(record,resolve);
        if(const auto found=previous.find(record.id);found!=previous.end()){
            if(next==projection.engine_components(*found->second,resolve))continue;
            const auto id=native.entity_for_uuid(uuid);if(id==quader::kInvalidEntityId)return false;
            edits.push_back({uuid,component_value(native,id),next});
        }else{
            CreatedEntity value;value.state.uuid=uuid;value.state.name=record.name;value.state.kind=quader::EntityKind::Generic;
            value.state.transform.parenting_offset.values=record.local.values;
            if(const auto fields=math::affineTransformFields(record.local);fields&&!fields->sheared&&!fields->singular){
                value.state.transform.location={fields->position[0],fields->position[1],fields->position[2]};
                value.state.transform.rotation_euler={fields->rotationRadians[0],fields->rotationRadians[1],fields->rotationRadians[2]};
                value.state.transform.scale={fields->scale[0],fields->scale[1],fields->scale[2]};value.state.transform.parenting_offset={};
            }
            value.state.visibility.visible=record.visible;
            value.state.engine_components=next;value.parent_uuid=resolve(record.parent);created.push_back(std::move(value));
        }
    }
    const auto next_settings=projection.engine_settings(after.properties,resolve);
    const bool settings_changed=next_settings!=projection.engine_settings(before.properties,resolve);
    if(edits.empty()&&created.empty()&&!settings_changed)return true;
    std::vector<std::string> selected;for(const auto id:state.level.document().selection().ids)selected.push_back(resolve(id));
    auto primary=resolve(state.level.document().selection().primary);
    if(!created.empty()){selected.clear();for(const auto& value:created)if(value.parent_uuid.empty())selected.push_back(value.state.uuid);if(!selected.empty())primary=selected.back();}
    const auto result=state.session->history().execute(std::make_unique<SceneComponentCommand>(native,label,std::move(edits),std::move(created),
        settings_changed,native.scene_settings().engine,next_settings,std::move(selected),primary));
    if(result.ok){state.native_revision=native.content_revision();state.accepted_edit=after;state.accepted_identities=std::move(identities);state.needs_reprojection=true;}
    return result.ok;
}
}
