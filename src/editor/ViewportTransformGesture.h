#pragma once

#include "scene/SceneDocument.h"

namespace crimson {
enum class ViewportTransformOperation { Translate,Rotate,Scale };

// A handle press owns a pending gesture, but authors nothing until the pointer
// crosses the drag threshold and produces a nonidentity world-space delta.
// The duplication modifier is captured at that initial press.
class ViewportTransformGesture final {
public:
    bool begin(SceneDocument&,std::vector<EntityId> roots,const Mat4& proxy,
               ViewportTransformOperation,bool shift);
    bool update(SceneDocument&,const Mat4& proxy,bool dragThresholdReached);
    bool finish(SceneDocument&);
    void cancel(SceneDocument&);
    bool active() const {return owner_!=nullptr;}
    bool editing() const {return transaction_!=0;}
    bool duplicating() const {return duplicate_;}
    bool valid(const SceneDocument&) const;
private:
    struct Root {EntityId id{},parent{};Mat4 world{},parentWorld{},parentInverse{};};
    SceneDocument* owner_{};
    std::uint64_t generation_{},revision_{},transaction_{};
    Mat4 inverseProxy_{};
    std::vector<Root> roots_;
    SceneSelection selection_;
    ViewportTransformOperation operation_{};
    bool duplicate_{};
    bool capture(const SceneDocument&,const std::vector<EntityId>&);
    void reset();
};
}
