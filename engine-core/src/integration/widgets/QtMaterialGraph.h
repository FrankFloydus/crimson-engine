#pragma once
#include "editor/MaterialDocument.h"
#include "editor/MaterialGraphActions.h"
#include <QGraphicsView>
#include <QImage>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace crimson {
class QtMaterialGraph final : public QGraphicsView {
public:
    explicit QtMaterialGraph(MaterialDocument&,QWidget* parent=nullptr);
    ~QtMaterialGraph() override;
    void refresh();
    void refreshImages();
    void releaseImages(){images_.clear();if(releaseImageCache)releaseImageCache();}
    void finishInteraction();
    void cancelInteraction();
    MaterialNodeId selectedNode()const;
    std::optional<QPoint> pinPosition(MaterialGraphEndpoint)const;
    void selectNodes(std::span<const MaterialNodeId>);
    std::optional<MaterialGraphPreviewTarget> previewTarget()const{return preview_;}
    void clearPreview(){preview_.reset();if(previewChanged)previewChanged(preview_);}
    std::function<void()> selectionChanged;
    std::function<void(std::optional<MaterialGraphPreviewTarget>)> previewChanged;
    std::function<QImage(MaterialGraphPreviewTarget)> nodeImage;
    std::function<void()> releaseImageCache;
    std::function<bool(const std::string&)> validTexture;
    std::function<void(MaterialNodeId)> chooseTexture;
protected:
    void contextMenuEvent(QContextMenuEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void mouseDoubleClickEvent(QMouseEvent*)override;
    void keyPressEvent(QKeyEvent*)override;
    void keyReleaseEvent(QKeyEvent*)override;
    void focusOutEvent(QFocusEvent*)override;
    void wheelEvent(QWheelEvent*)override;
    void dragEnterEvent(QDragEnterEvent*)override;
    void dragMoveEvent(QDragMoveEvent*)override;
    void dropEvent(QDropEvent*)override;
private:
    MaterialDocument& document_;
    std::uint64_t revision_{UINT64_MAX};
    std::optional<MaterialGraphPreviewTarget> preview_;
    std::optional<MaterialGraphEndpoint> connecting_;
    QGraphicsPathItem* temporary_{};
    bool moving_{},inlineEditing_{},panning_{},panned_{},creationGesture_{};
    QPointF press_;
    QPoint panPoint_;
    std::set<int> held_;
    std::map<MaterialNodeId,QImage> images_;
    std::vector<MaterialNodeId> selectedNodes()const;
    std::optional<MaterialGraphEndpoint> pinAt(const QPoint&)const;
    void deleteSelection();
    void copy();
    void paste();
    void search(QPointF,std::optional<MaterialGraphEndpoint> from={});
    void arrange(int axis,int alignment,bool distribute);
    void straighten();
    void disconnect(std::optional<MaterialGraphEndpoint> endpoint={});
    void togglePreview(MaterialNodeId,std::optional<MaterialPin> pin={});
    void updateConnections();
    void editNode(MaterialNodeId,const std::string&,const std::function<void(MaterialNode&)>&,bool final=false);
};
}
