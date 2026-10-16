#include "ViewportTransformGesture.h"

#include <algorithm>
#include <cmath>

namespace crimson {
namespace {
bool meaningful(const Mat4& delta) {
    constexpr float Epsilon=1e-6f;
    for(std::size_t row=0;row<4;++row)for(std::size_t column=0;column<4;++column)
        if(std::abs(delta.at(row,column)-(row==column?1.f:0.f))>Epsilon)return true;
    return false;
}
}
bool ViewportTransformGesture::capture(const SceneDocument& document,const std::vector<EntityId>& ids) {
    std::vector<Root> roots;roots.reserve(ids.size());
    for(const auto id:ids) {
        const auto record=document.scene().find(id);
        const auto world=document.scene().worldTransform(id);
        const auto parent=record && record->parent?document.scene().worldTransform(record->parent):std::optional{Mat4{}};
        const auto inverse=parent?math::inverse(*parent):std::nullopt;
        if(!record || !world || !inverse)return false;
        roots.push_back({id,record->parent,*world,*parent,*inverse});
    }
    if(roots.empty())return false;
    roots_=std::move(roots);return true;
}
bool ViewportTransformGesture::begin(SceneDocument& document,std::vector<EntityId> ids,const Mat4& proxy,
                                    ViewportTransformOperation operation,bool shift) {
    if(active() || document.transactionActive() || !math::finite(proxy) || !math::isAffine(proxy))return false;
    const auto inverse=math::inverse(proxy);
    if(!inverse || !capture(document,document.scene().topmost(ids)))return false;
    owner_=&document;generation_=document.documentGeneration();revision_=document.scene().revision();
    transaction_=0;inverseProxy_=*inverse;selection_=document.selection();operation_=operation;
    duplicate_=shift && operation!=ViewportTransformOperation::Scale;return true;
}
bool ViewportTransformGesture::valid(const SceneDocument& document) const {
    if(owner_!=&document || generation_!=document.documentGeneration() || revision_!=document.scene().revision() ||
        selection_.ids!=document.selection().ids || selection_.primary!=document.selection().primary ||
        document.transactionId()!=transaction_)return false;
    for(const auto& root:roots_) {
        const auto record=document.scene().find(root.id);
        if(!record || record->parent!=root.parent)return false;
        const auto parent=root.parent?document.scene().worldTransform(root.parent):std::optional{Mat4{}};
        if(!parent || *parent!=root.parentWorld)return false;
    }
    return !roots_.empty();
}
bool ViewportTransformGesture::update(SceneDocument& document,const Mat4& proxy,bool dragThresholdReached) {
    if(!valid(document)){cancel(document);return false;}
    if(!math::finite(proxy) || !math::isAffine(proxy))return true;
    const auto delta=proxy*inverseProxy_;
    if(!math::finite(delta) || !math::isAffine(delta))return true;
    if(!editing()) {
        if(!dragThresholdReached || !meaningful(delta))return true;
        std::vector<EntityId> ids;ids.reserve(roots_.size());
        for(const auto& root:roots_)ids.push_back(root.id);
        const char* label=duplicate_?(operation_==ViewportTransformOperation::Rotate?
            "Duplicate and rotate selection":"Duplicate and move selection"):"Transform selection";
        const bool begun=duplicate_?document.beginDuplicateTransform(label,std::move(ids)):
            document.beginTransform(label,std::move(ids));
        if(!begun){reset();return false;}
        transaction_=document.transactionId();revision_=document.scene().revision();selection_=document.selection();
        if(duplicate_ && !capture(document,document.transformEditRoots())){cancel(document);return false;}
    }
    const bool updated=document.update([&](auto& records,auto&) {
        for(const auto& root:roots_) {
            const auto record=std::find_if(records.begin(),records.end(),[&](const auto& candidate){return candidate.id==root.id;});
            if(record!=records.end())record->local=root.parentInverse*delta*root.world;
        }
    });
    revision_=document.scene().revision();
    return updated;
}
bool ViewportTransformGesture::finish(SceneDocument& document) {
    if(!valid(document)){cancel(document);return false;}
    const bool result=!editing() || document.commit();reset();return result;
}
void ViewportTransformGesture::cancel(SceneDocument& document) {
    if(owner_==&document && generation_==document.documentGeneration() && transaction_ &&
        document.transactionId()==transaction_)document.cancel();
    reset();
}
void ViewportTransformGesture::reset() {
    owner_=nullptr;generation_=revision_=transaction_=0;roots_.clear();selection_={};duplicate_=false;
}
}
