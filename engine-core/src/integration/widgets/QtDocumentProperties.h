#pragma once

#include <QTreeWidget>
#include <QPointer>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>

class QAction;
class QCloseEvent;
class QHideEvent;
class QShowEvent;
class QLabel;
class QToolBar;
class QSplitter;

namespace crimson {
// Retained property controls project a document snapshot. Only the document
// callback mutates authoring state; focus gestures remain one history command.
class QtDocumentProperties final : public QTreeWidget {
public:
    using Json=nlohmann::json;
    using Read=std::function<Json()>;
    using Write=std::function<void(const Json&,const std::string&,bool)>;
    explicit QtDocumentProperties(QWidget* parent=nullptr);
    ~QtDocumentProperties() override;
    void bind(Read,Write,std::function<void()> finish,std::function<void()> cancel);
    void refresh(std::uint64_t revision,bool editable=true);
    void setChoices(std::function<QStringList(const std::string&)> choices) { choices_=std::move(choices); }
    void setRootFilter(std::function<bool(const std::string&)> filter) { filter_=std::move(filter);revision_=UINT64_MAX; }
    void finish();
    void cancel();
    void resetProjection() { editing_=false;revision_=UINT64_MAX; }
    void detach() { read_={};write_={};finish_={};cancel_={};editing_=false; }
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    Read read_;
    Write write_;
    std::function<void()> finish_,cancel_;
    std::function<QStringList(const std::string&)> choices_;
    std::function<bool(const std::string&)> filter_;
    std::unique_ptr<QObject> controlsLifetime_;
    std::uint64_t revision_{UINT64_MAX};
    bool rebuilding_{},editing_{};
    void append(QTreeWidgetItem*,const Json&,const Json::json_pointer&,const std::string&);
    void updateValue(const Json::json_pointer&,const Json&,const std::string&,bool);
};

}
