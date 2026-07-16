////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_project_home_page.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QModelIndex>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTableView>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace quader {

QtProjectHomeLabels QtProjectHomeLabels::defaults() {
  return {
      .new_project = QStringLiteral("New Quader Project"),
      .open_project = QStringLiteral("Open Project"),
      .recent_projects = QStringLiteral("Recent Projects"),
      .recent_name_column = QStringLiteral("Name"),
      .recent_location_column = QStringLiteral("Location"),
      .no_recent_projects = QStringLiteral("No recent projects"),
      .unavailable_project = QStringLiteral("Project is unavailable"),
      .remove_recent_project = QStringLiteral("Remove from Recent"),
  };
}

QtRecentProjectsModel::QtRecentProjectsModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int QtRecentProjectsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(projects_.size());
}

int QtRecentProjectsModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant QtRecentProjectsModel::data(const QModelIndex &index,
                                     int role) const {
  const QtRecentProjectItem *project = project_at(index.row());
  if (project == nullptr || !index.isValid() ||
      index.column() < 0 || index.column() >= ColumnCount) {
    return {};
  }

  if (role == ManifestPathRole) {
    return project->manifest_path;
  }
  if (role == AvailableRole) {
    return project->available;
  }
  if (role == Qt::ToolTipRole) {
    if (!project->available) {
      return QStringLiteral("%1\n%2")
          .arg(project->manifest_path, labels_.unavailable_project);
    }
    return project->manifest_path;
  }
  if (role == Qt::AccessibleDescriptionRole && !project->available) {
    return labels_.unavailable_project;
  }
  if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) {
    return {};
  }

  if (index.column() == NameColumn) {
    return project->available
               ? project->name
               : QStringLiteral("%1 (%2)")
                     .arg(project->name, labels_.unavailable_project);
  }
  return project->manifest_path;
}

QVariant QtRecentProjectsModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  if (section == NameColumn) {
    return labels_.recent_name_column;
  }
  if (section == LocationColumn) {
    return labels_.recent_location_column;
  }
  return {};
}

Qt::ItemFlags QtRecentProjectsModel::flags(const QModelIndex &index) const {
  const QtRecentProjectItem *project = project_at(index.row());
  if (!index.isValid() || project == nullptr) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void QtRecentProjectsModel::set_labels(QtProjectHomeLabels labels) {
  labels_ = std::move(labels);
  emit headerDataChanged(Qt::Horizontal, NameColumn, LocationColumn);
  if (!projects_.empty()) {
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(projects_.size()) - 1,
                           ColumnCount - 1),
                     {Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
  }
}

void QtRecentProjectsModel::set_projects(
    std::vector<QtRecentProjectItem> projects) {
  if (projects.size() > kQtProjectHomeMaximumRecentProjects) {
    projects.resize(kQtProjectHomeMaximumRecentProjects);
  }
  if (projects_ == projects) {
    return;
  }
  beginResetModel();
  projects_ = std::move(projects);
  endResetModel();
}

const QtRecentProjectItem *
QtRecentProjectsModel::project_at(int row) const {
  if (row < 0 || row >= static_cast<int>(projects_.size())) {
    return nullptr;
  }
  return &projects_[static_cast<std::size_t>(row)];
}

QtProjectHomePage::QtProjectHomePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("projectHome"));

  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(32, 32, 32, 32);
  root_layout->setSpacing(16);

  auto *project_actions = new QHBoxLayout();
  project_actions->setSpacing(8);
  new_project_button_ = new QPushButton(this);
  new_project_button_->setObjectName(
      QStringLiteral("projectHome.newProject"));
  new_project_button_->setDefault(true);
  project_actions->addWidget(new_project_button_);

  open_project_button_ = new QPushButton(this);
  open_project_button_->setObjectName(
      QStringLiteral("projectHome.openProject"));
  project_actions->addWidget(open_project_button_);
  project_actions->addStretch(1);
  root_layout->addLayout(project_actions);

  recent_projects_label_ = new QLabel(this);
  recent_projects_label_->setObjectName(
      QStringLiteral("projectHome.recentProjects.label"));
  root_layout->addWidget(recent_projects_label_);

  recent_projects_stack_ = new QStackedWidget(this);
  recent_projects_stack_->setObjectName(
      QStringLiteral("projectHome.recentProjects.stack"));

  recent_projects_view_ = new QTableView(recent_projects_stack_);
  recent_projects_view_->setObjectName(
      QStringLiteral("projectHome.recentProjects.view"));
  recent_projects_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  recent_projects_view_->setSelectionBehavior(
      QAbstractItemView::SelectRows);
  recent_projects_view_->setSelectionMode(
      QAbstractItemView::SingleSelection);
  recent_projects_view_->setShowGrid(false);
  recent_projects_view_->setWordWrap(false);
  recent_projects_view_->verticalHeader()->hide();
  recent_projects_view_->horizontalHeader()->setStretchLastSection(true);

  recent_projects_model_ =
      new QtRecentProjectsModel(recent_projects_view_);
  recent_projects_model_->setObjectName(
      QStringLiteral("projectHome.recentProjects.model"));
  recent_projects_view_->setModel(recent_projects_model_);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::NameColumn, QHeaderView::ResizeToContents);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::LocationColumn, QHeaderView::Stretch);
  recent_projects_stack_->addWidget(recent_projects_view_);

  no_recent_projects_label_ = new QLabel(recent_projects_stack_);
  no_recent_projects_label_->setObjectName(
      QStringLiteral("projectHome.recentProjects.empty"));
  no_recent_projects_label_->setAlignment(Qt::AlignCenter);
  no_recent_projects_label_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Expanding);
  recent_projects_stack_->addWidget(no_recent_projects_label_);
  root_layout->addWidget(recent_projects_stack_, 1);

  remove_recent_project_button_ = new QPushButton(this);
  remove_recent_project_button_->setObjectName(
      QStringLiteral("projectHome.removeRecent"));
  remove_recent_project_button_->setEnabled(false);
  root_layout->addWidget(remove_recent_project_button_, 0, Qt::AlignLeft);

  QObject::connect(new_project_button_, &QPushButton::clicked, this,
                   [this]() {
                     if (new_project_requested_) {
                       new_project_requested_();
                     }
                   });
  QObject::connect(open_project_button_, &QPushButton::clicked, this,
                   [this]() {
                     if (open_project_requested_) {
                       open_project_requested_();
                     }
                   });
  QObject::connect(recent_projects_view_, &QTableView::activated, this,
                   [this](const QModelIndex &index) {
                     dispatch_recent_project_open(index);
                   });
  QObject::connect(recent_projects_view_->selectionModel(),
                   &QItemSelectionModel::selectionChanged, this,
                   [this]() { refresh_remove_recent_button(); });
  QObject::connect(remove_recent_project_button_, &QPushButton::clicked, this,
                   [this]() { dispatch_recent_project_removal(); });

  refresh_labels();
  refresh_recent_projects_page();
}

void QtProjectHomePage::set_labels(QtProjectHomeLabels labels) {
  labels_ = std::move(labels);
  recent_projects_model_->set_labels(labels_);
  refresh_labels();
}

void QtProjectHomePage::set_recent_projects(
    std::vector<QtRecentProjectItem> projects) {
  recent_projects_model_->set_projects(std::move(projects));
  refresh_recent_projects_page();
  refresh_remove_recent_button();
}

void QtProjectHomePage::set_new_project_requested_callback(
    RequestCallback callback) {
  new_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_open_project_requested_callback(
    RequestCallback callback) {
  open_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_open_recent_project_requested_callback(
    OpenRecentProjectCallback callback) {
  open_recent_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_remove_recent_project_requested_callback(
    RemoveRecentProjectCallback callback) {
  remove_recent_project_requested_ = std::move(callback);
}

void QtProjectHomePage::dispatch_recent_project_open(
    const QModelIndex &index) {
  const QtRecentProjectItem *project =
      recent_projects_model_->project_at(index.row());
  if (project == nullptr || !project->available ||
      !open_recent_project_requested_) {
    return;
  }
  const QString manifest_path = project->manifest_path;
  open_recent_project_requested_(manifest_path);
}

void QtProjectHomePage::dispatch_recent_project_removal() {
  if (!remove_recent_project_requested_) {
    return;
  }
  const QModelIndex index = recent_projects_view_->currentIndex();
  const QtRecentProjectItem *project =
      recent_projects_model_->project_at(index.row());
  if (project != nullptr) {
    remove_recent_project_requested_(project->manifest_path);
  }
}

void QtProjectHomePage::refresh_remove_recent_button() {
  if (remove_recent_project_button_ != nullptr) {
    remove_recent_project_button_->setEnabled(
        recent_projects_view_ != nullptr &&
        recent_projects_view_->currentIndex().isValid());
  }
}

void QtProjectHomePage::refresh_labels() {
  new_project_button_->setText(labels_.new_project);
  new_project_button_->setAccessibleName(labels_.new_project);
  open_project_button_->setText(labels_.open_project);
  open_project_button_->setAccessibleName(labels_.open_project);
  recent_projects_label_->setText(labels_.recent_projects);
  no_recent_projects_label_->setText(labels_.no_recent_projects);
  remove_recent_project_button_->setText(labels_.remove_recent_project);
}

void QtProjectHomePage::refresh_recent_projects_page() {
  const bool has_recent_projects =
      recent_projects_model_->rowCount() != 0;
  recent_projects_stack_->setCurrentWidget(
      has_recent_projects ? static_cast<QWidget *>(recent_projects_view_)
                          : static_cast<QWidget *>(no_recent_projects_label_));
}

} // namespace quader
