////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_scene_workspace.hpp"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace quader {
namespace {

QString normalized_relative_path(QString path) {
  path.replace(u'\\', u'/');
  while (path.startsWith(u'/')) {
    path.remove(0, 1);
  }
  return path;
}

QString scene_base_name(const QtSceneTabItem &scene,
                        const QString &untitled_scene) {
  const QString explicit_name = scene.name.trimmed();
  if (!explicit_name.isEmpty()) {
    return explicit_name;
  }
  const QString path = normalized_relative_path(scene.project_relative_path);
  if (!path.isEmpty()) {
    const QString file_name = QFileInfo(path).completeBaseName();
    if (!file_name.isEmpty()) {
      return file_name;
    }
  }
  return untitled_scene;
}

QStringList parent_components(const QtSceneTabItem &scene) {
  QStringList parts = normalized_relative_path(scene.project_relative_path)
                          .split(u'/', Qt::SkipEmptyParts);
  if (!parts.isEmpty()) {
    parts.removeLast();
  }
  return parts;
}

QString parent_suffix(const QStringList &components, int depth) {
  const int count = std::min(depth, static_cast<int>(components.size()));
  if (count <= 0) {
    return {};
  }
  return QStringList(components.cend() - count, components.cend()).join(u'/');
}

QString case_key(const QString &text) { return text.toCaseFolded(); }

} // namespace

QtSceneWorkspaceLabels QtSceneWorkspaceLabels::defaults() {
  return {
      .untitled_scene = QStringLiteral("Untitled Scene"),
      .unsaved_scene = QStringLiteral("Unsaved scene"),
      .new_scene = QStringLiteral("New Scene"),
      .open_scene = QStringLiteral("Open Scene"),
      .empty_title = QStringLiteral("No scenes open"),
      .empty_description =
          QStringLiteral("Create a new scene or open one from this project."),
      .return_to_scene = QStringLiteral("Return to scene %1"),
      .editing_instance = QStringLiteral("Editing instance %1"),
  };
}

std::vector<QString> qt_scene_tab_display_labels(
    std::span<const QtSceneTabItem> scenes, const QString &untitled_scene) {
  std::vector<QString> base_names;
  std::vector<QStringList> parents;
  base_names.reserve(scenes.size());
  parents.reserve(scenes.size());
  for (const QtSceneTabItem &scene : scenes) {
    base_names.push_back(scene_base_name(scene, untitled_scene));
    parents.push_back(parent_components(scene));
  }

  std::vector<QString> labels = base_names;
  for (std::size_t index = 0; index < scenes.size(); ++index) {
    std::vector<std::size_t> matching_indices;
    for (std::size_t candidate = 0; candidate < scenes.size(); ++candidate) {
      if (case_key(base_names[candidate]) == case_key(base_names[index])) {
        matching_indices.push_back(candidate);
      }
    }
    if (matching_indices.size() <= 1) {
      continue;
    }

    int maximum_depth = 0;
    for (const std::size_t candidate : matching_indices) {
      maximum_depth =
          std::max(maximum_depth, static_cast<int>(parents[candidate].size()));
    }

    bool distinguished = false;
    for (int depth = 1; depth <= maximum_depth; ++depth) {
      const QString own_suffix = parent_suffix(parents[index], depth);
      bool unique = true;
      for (const std::size_t candidate : matching_indices) {
        if (candidate == index) {
          continue;
        }
        if (case_key(parent_suffix(parents[candidate], depth)) ==
            case_key(own_suffix)) {
          unique = false;
          break;
        }
      }
      if (!unique) {
        continue;
      }
      labels[index] = own_suffix.isEmpty()
                          ? base_names[index]
                          : QStringLiteral("%1/%2")
                                .arg(own_suffix, base_names[index]);
      distinguished = true;
      break;
    }

    if (!distinguished) {
      const auto position =
          std::find(matching_indices.begin(), matching_indices.end(), index);
      const int duplicate_number =
          static_cast<int>(std::distance(matching_indices.begin(), position)) +
          1;
      labels[index] = QStringLiteral("%1 (%2)")
                          .arg(base_names[index])
                          .arg(duplicate_number);
    }
  }

  for (std::size_t index = 0; index < scenes.size(); ++index) {
    if (scenes[index].dirty) {
      labels[index].append(QStringLiteral(" *"));
    }
  }
  return labels;
}

QString qt_scene_tab_tooltip(const QtSceneTabItem &scene,
                             const QString &unsaved_scene) {
  const QString path = normalized_relative_path(scene.project_relative_path);
  return path.isEmpty() ? unsaved_scene : path;
}

QtSceneWorkspace::QtSceneWorkspace(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("sceneWorkspace"));

  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  auto *tabs_row = new QWidget(this);
  tabs_row->setObjectName(QStringLiteral("sceneWorkspace.tabsRow"));
  auto *tabs_layout = new QHBoxLayout(tabs_row);
  tabs_layout->setContentsMargins(0, 0, 0, 0);
  tabs_layout->setSpacing(0);

  tab_bar_ = new QTabBar(tabs_row);
  tab_bar_->setObjectName(QStringLiteral("sceneWorkspace.tabs"));
  tab_bar_->setDocumentMode(true);
  tab_bar_->setDrawBase(true);
  tab_bar_->setTabsClosable(true);
  tab_bar_->setMovable(false);
  tab_bar_->setExpanding(false);
  tab_bar_->setElideMode(Qt::ElideRight);
  tab_bar_->setUsesScrollButtons(true);
  tabs_layout->addWidget(tab_bar_, 1);

  new_scene_button_ = new QToolButton(tabs_row);
  new_scene_button_->setObjectName(
      QStringLiteral("sceneWorkspace.newScene"));
  new_scene_button_->setText(QStringLiteral("+"));
  new_scene_button_->setAutoRaise(true);
  tabs_layout->addWidget(new_scene_button_);
  root_layout->addWidget(tabs_row);

  instance_edit_bar_ = new QWidget(this);
  instance_edit_bar_->setObjectName(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb"));
  instance_edit_layout_ = new QHBoxLayout(instance_edit_bar_);
  instance_edit_layout_->setContentsMargins(6, 2, 6, 2);
  instance_edit_layout_->setSpacing(4);

  instance_scene_crumb_ = new QToolButton(instance_edit_bar_);
  instance_scene_crumb_->setObjectName(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.scene"));
  instance_scene_crumb_->setAutoRaise(true);
  instance_edit_layout_->addWidget(instance_scene_crumb_);

  instance_separator_ = new QLabel(QStringLiteral("\u203a"),
                                   instance_edit_bar_);
  instance_separator_->setObjectName(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.separator"));
  instance_edit_layout_->addWidget(instance_separator_);

  instance_name_crumb_ = new QLabel(instance_edit_bar_);
  instance_name_crumb_->setObjectName(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.instance"));
  instance_edit_layout_->addWidget(instance_name_crumb_);
  instance_edit_layout_->addStretch(1);
  instance_edit_bar_->hide();
  root_layout->addWidget(instance_edit_bar_);

  content_stack_ = new QStackedWidget(this);
  content_stack_->setObjectName(QStringLiteral("sceneWorkspace.content"));

  empty_page_ = new QWidget(content_stack_);
  empty_page_->setObjectName(QStringLiteral("sceneWorkspace.empty"));
  auto *empty_root = new QVBoxLayout(empty_page_);
  empty_root->addStretch(1);

  empty_title_label_ = new QLabel(empty_page_);
  empty_title_label_->setObjectName(
      QStringLiteral("sceneWorkspace.empty.title"));
  empty_title_label_->setAlignment(Qt::AlignCenter);
  empty_root->addWidget(empty_title_label_);

  empty_description_label_ = new QLabel(empty_page_);
  empty_description_label_->setObjectName(
      QStringLiteral("sceneWorkspace.empty.description"));
  empty_description_label_->setAlignment(Qt::AlignCenter);
  empty_description_label_->setWordWrap(true);
  empty_root->addWidget(empty_description_label_);

  auto *empty_actions = new QHBoxLayout();
  empty_actions->addStretch(1);
  empty_new_scene_button_ = new QPushButton(empty_page_);
  empty_new_scene_button_->setObjectName(
      QStringLiteral("sceneWorkspace.empty.newScene"));
  empty_actions->addWidget(empty_new_scene_button_);
  empty_open_scene_button_ = new QPushButton(empty_page_);
  empty_open_scene_button_->setObjectName(
      QStringLiteral("sceneWorkspace.empty.openScene"));
  empty_actions->addWidget(empty_open_scene_button_);
  empty_actions->addStretch(1);
  empty_root->addLayout(empty_actions);
  empty_root->addStretch(1);
  content_stack_->addWidget(empty_page_);

  viewport_page_ = new QWidget(content_stack_);
  viewport_page_->setObjectName(QStringLiteral("sceneWorkspace.viewport"));
  viewport_layout_ = new QVBoxLayout(viewport_page_);
  viewport_layout_->setContentsMargins(0, 0, 0, 0);
  viewport_layout_->setSpacing(0);
  content_stack_->addWidget(viewport_page_);
  root_layout->addWidget(content_stack_, 1);

  const auto dispatch_new_scene = [this]() {
    if (new_scene_requested_) {
      new_scene_requested_();
    }
  };
  QObject::connect(new_scene_button_, &QToolButton::clicked, this,
                   dispatch_new_scene);
  QObject::connect(empty_new_scene_button_, &QPushButton::clicked, this,
                   dispatch_new_scene);
  QObject::connect(empty_open_scene_button_, &QPushButton::clicked, this,
                   [this]() {
                     if (open_scene_requested_) {
                       open_scene_requested_();
                     }
                   });
  QObject::connect(tab_bar_, &QTabBar::currentChanged, this,
                   [this](int index) {
                     const auto session_id = session_id_at(index);
                     if (session_id && activate_scene_requested_) {
                       activate_scene_requested_(*session_id);
                     }
                   });
  QObject::connect(tab_bar_, &QTabBar::tabCloseRequested, this,
                   [this](int index) {
                     const auto session_id = session_id_at(index);
                     if (session_id && close_scene_requested_) {
                       close_scene_requested_(*session_id);
                     }
                   });
  QObject::connect(instance_scene_crumb_, &QToolButton::clicked, this,
                   [this]() {
                     if (exit_instance_edit_requested_) {
                       exit_instance_edit_requested_();
                     }
                   });

  apply_ui_scale(QtUiScaleMetrics{});
  refresh_labels();
  refresh_instance_edit_breadcrumb();
  refresh_content_page();
}

void QtSceneWorkspace::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (applied_ui_scale_revision_ == metrics.revision() &&
      applied_ui_scale_factor_ == metrics.factor()) {
    return;
  }
  applied_ui_scale_revision_ = metrics.revision();
  applied_ui_scale_factor_ = metrics.factor();
  instance_edit_layout_->setContentsMargins(
      metrics.margins(QMargins(6, 2, 6, 2)));
  instance_edit_layout_->setSpacing(metrics.px(4));
  instance_edit_layout_->invalidate();
  updateGeometry();
}

void QtSceneWorkspace::set_labels(QtSceneWorkspaceLabels labels) {
  labels_ = std::move(labels);
  refresh_labels();
  rebuild_tabs(active_scene());
}

void QtSceneWorkspace::set_viewport_widget(QWidget *viewport_widget) {
  if (viewport_widget_ == viewport_widget) {
    return;
  }
  if (viewport_widget_ != nullptr) {
    viewport_layout_->removeWidget(viewport_widget_);
    viewport_widget_->hide();
    viewport_widget_->setParent(nullptr);
  }
  viewport_widget_ = viewport_widget;
  if (viewport_widget_ != nullptr) {
    viewport_layout_->addWidget(viewport_widget_);
  }
  refresh_content_page();
}

bool QtSceneWorkspace::set_scene_tabs(
    std::vector<QtSceneTabItem> scenes,
    std::optional<QtSceneSessionId> active_scene) {
  QSet<qulonglong> identities;
  for (const QtSceneTabItem &scene : scenes) {
    if (scene.session_id == kInvalidQtSceneSessionId ||
        identities.contains(static_cast<qulonglong>(scene.session_id))) {
      return false;
    }
    identities.insert(static_cast<qulonglong>(scene.session_id));
  }
  if (active_scene && !scenes.empty()) {
    const bool active_exists =
        std::ranges::any_of(scenes, [active_scene](const QtSceneTabItem &item) {
          return item.session_id == *active_scene;
        });
    if (!active_exists) {
      return false;
    }
  }
  if (active_scene && scenes.empty()) {
    return false;
  }

  scenes_ = std::move(scenes);
  rebuild_tabs(active_scene);
  refresh_content_page();
  return true;
}

bool QtSceneWorkspace::set_active_scene(QtSceneSessionId session_id) {
  const int index = tab_index(session_id);
  if (index < 0) {
    return false;
  }
  const QSignalBlocker blocker(tab_bar_);
  tab_bar_->setCurrentIndex(index);
  return true;
}

void QtSceneWorkspace::set_new_scene_requested_callback(
    RequestCallback callback) {
  new_scene_requested_ = std::move(callback);
}

void QtSceneWorkspace::set_open_scene_requested_callback(
    RequestCallback callback) {
  open_scene_requested_ = std::move(callback);
}

void QtSceneWorkspace::set_activate_scene_requested_callback(
    SceneRequestCallback callback) {
  activate_scene_requested_ = std::move(callback);
}

void QtSceneWorkspace::set_close_scene_requested_callback(
    SceneRequestCallback callback) {
  close_scene_requested_ = std::move(callback);
}

void QtSceneWorkspace::set_exit_instance_edit_requested_callback(
    RequestCallback callback) {
  exit_instance_edit_requested_ = std::move(callback);
}

void QtSceneWorkspace::set_instance_edit_breadcrumb(
    std::optional<QtInstanceEditBreadcrumb> breadcrumb) {
  if (instance_edit_breadcrumb_ == breadcrumb) {
    return;
  }
  instance_edit_breadcrumb_ = std::move(breadcrumb);
  refresh_instance_edit_breadcrumb();
}

std::optional<QtSceneSessionId> QtSceneWorkspace::active_scene() const {
  return session_id_at(tab_bar_->currentIndex());
}

int QtSceneWorkspace::tab_index(QtSceneSessionId session_id) const {
  for (int index = 0; index < tab_bar_->count(); ++index) {
    const auto candidate = session_id_at(index);
    if (candidate && *candidate == session_id) {
      return index;
    }
  }
  return -1;
}

std::optional<QtSceneSessionId>
QtSceneWorkspace::session_id_at(int index) const {
  if (index < 0 || index >= tab_bar_->count()) {
    return std::nullopt;
  }
  bool converted = false;
  const qulonglong value = tab_bar_->tabData(index).toULongLong(&converted);
  if (!converted || value == kInvalidQtSceneSessionId ||
      value > std::numeric_limits<QtSceneSessionId>::max()) {
    return std::nullopt;
  }
  return static_cast<QtSceneSessionId>(value);
}

void QtSceneWorkspace::rebuild_tabs(
    std::optional<QtSceneSessionId> active_scene) {
  if (!active_scene) {
    active_scene = this->active_scene();
  }
  const QSignalBlocker blocker(tab_bar_);
  while (tab_bar_->count() > 0) {
    tab_bar_->removeTab(tab_bar_->count() - 1);
  }

  const std::vector<QString> labels =
      qt_scene_tab_display_labels(scenes_, labels_.untitled_scene);
  for (std::size_t index = 0; index < scenes_.size(); ++index) {
    const QtSceneTabItem &scene = scenes_[index];
    const int tab_index = tab_bar_->addTab(labels[index]);
    tab_bar_->setTabData(
        tab_index,
        QVariant::fromValue(static_cast<qulonglong>(scene.session_id)));
    tab_bar_->setTabToolTip(
        tab_index, qt_scene_tab_tooltip(scene, labels_.unsaved_scene));
    tab_bar_->setTabWhatsThis(
        tab_index, qt_scene_tab_tooltip(scene, labels_.unsaved_scene));
  }

  int active_index = -1;
  if (active_scene) {
    active_index = tab_index(*active_scene);
  }
  if (active_index < 0 && tab_bar_->count() > 0) {
    active_index = 0;
  }
  tab_bar_->setCurrentIndex(active_index);
}

void QtSceneWorkspace::refresh_labels() {
  new_scene_button_->setToolTip(labels_.new_scene);
  new_scene_button_->setAccessibleName(labels_.new_scene);
  empty_title_label_->setText(labels_.empty_title);
  empty_description_label_->setText(labels_.empty_description);
  empty_new_scene_button_->setText(labels_.new_scene);
  empty_new_scene_button_->setAccessibleName(labels_.new_scene);
  empty_open_scene_button_->setText(labels_.open_scene);
  empty_open_scene_button_->setAccessibleName(labels_.open_scene);
  refresh_instance_edit_breadcrumb();
}

void QtSceneWorkspace::refresh_instance_edit_breadcrumb() {
  const bool visible = instance_edit_breadcrumb_.has_value();
  instance_edit_bar_->setVisible(visible);
  if (!visible) {
    instance_scene_crumb_->setText({});
    instance_name_crumb_->clear();
    return;
  }

  const QtInstanceEditBreadcrumb &breadcrumb = *instance_edit_breadcrumb_;
  instance_scene_crumb_->setText(breadcrumb.scene_name);
  const QString return_text = labels_.return_to_scene.arg(
      breadcrumb.scene_name);
  instance_scene_crumb_->setToolTip(return_text);
  instance_scene_crumb_->setAccessibleName(return_text);

  instance_name_crumb_->setText(breadcrumb.instance_name);
  const QString editing_text = labels_.editing_instance.arg(
      breadcrumb.instance_name);
  instance_name_crumb_->setToolTip(editing_text);
  instance_name_crumb_->setAccessibleName(editing_text);
}

void QtSceneWorkspace::refresh_content_page() {
  const bool show_viewport = !scenes_.empty() && viewport_widget_ != nullptr;
  content_stack_->setCurrentWidget(show_viewport ? viewport_page_
                                                 : empty_page_);
  if (viewport_widget_ != nullptr) {
    viewport_widget_->setVisible(show_viewport);
  }
}

} // namespace quader
