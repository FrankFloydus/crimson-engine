#pragma once
#include "editor/CollisionLayersDocument.h"
#include "physics/CollisionFilter.h"
#include <QComboBox>
#include <functional>

namespace crimson {
class QtCollisionMaskControl final : public QComboBox {
public:
    explicit QtCollisionMaskControl(QWidget* parent=nullptr);
    ~QtCollisionMaskControl() override;
    void synchronize(std::uint32_t,const CollisionLayerNames&,std::uint32_t editableBits=AllCollisionLayers);
    std::uint32_t value()const{return value_;}
    void resetTo(std::uint32_t defaultValue=AllCollisionLayers);
    std::function<void(std::uint32_t)> changed;
protected:
    bool eventFilter(QObject*,QEvent*)override;
    void paintEvent(QPaintEvent*)override;
private:
    std::uint32_t value_{},editable_{AllCollisionLayers};
    bool syncing_{};
    QMetaObject::Connection itemConnection_;
    void publish(std::uint32_t);
};
}
