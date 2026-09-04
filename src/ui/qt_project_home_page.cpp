////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_project_home_page.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QModelIndex>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QTableView>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace quader {
namespace {

QString unavailable_metadata_text() {
  return QStringLiteral("\u2014");
}

QString format_local_timestamp(
    const std::optional<std::uint64_t> unix_milliseconds) {
  if (!unix_milliseconds.has_value()) {
    return unavailable_metadata_text();
  }
  const std::uint64_t maximum_qint64 =
      static_cast<std::uint64_t>(std::numeric_limits<qint64>::max());
  const qint64 clamped_unix_milliseconds = static_cast<qint64>(
      std::min(*unix_milliseconds, maximum_qint64));
  const QDateTime local_time =
      QDateTime::fromMSecsSinceEpoch(clamped_unix_milliseconds)
          .toLocalTime();
  return QLocale().toString(local_time, QLocale::ShortFormat);
}

QString format_manifest_size(
    const std::optional<std::uintmax_t> size_bytes) {
  if (!size_bytes.has_value()) {
    return unavailable_metadata_text();
  }
  const std::uintmax_t maximum_qint64 =
      static_cast<std::uintmax_t>(std::numeric_limits<qint64>::max());
  return QLocale().formattedDataSize(
      static_cast<qint64>(std::min(*size_bytes, maximum_qint64)), 2,
      QLocale::DataSizeTraditionalFormat);
}

} // namespace

QtProjectHomeLabels QtProjectHomeLabels::defaults() {
  return {
      .new_project = QStringLiteral("New"),
      .open_project = QStringLiteral("Open"),
      .recent_projects = QStringLiteral("Recent"),
      .recent_name_column = QStringLiteral("Name"),
      .recent_last_modified_column = QStringLiteral("Last modified"),
      .recent_size_column = QStringLiteral("Size"),
      .recent_actions_column = QStringLiteral("Actions"),
      .no_recent_projects = QStringLiteral("No recent projects"),
      .unavailable_project = QStringLiteral("Project is unavailable"),
      .open_recent_project = QStringLiteral("Open"),
      .open_recent_project_location =
          QStringLiteral("Open file location"),
      .copy_recent_project_path = QStringLiteral("Copy path"),
      .remove_recent_project = QStringLiteral("Remove from recent"),
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
  if (role == Qt::TextAlignmentRole &&
      index.column() == ActionsColumn) {
    return Qt::AlignCenter;
  }
  if (role == Qt::ToolTipRole) {
    if (index.column() == ActionsColumn) {
      return labels_.recent_actions_column;
    }
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

  if (index.column() == ActionsColumn) {
    return role == Qt::DisplayRole ? QStringLiteral("\u2026")
                                   : labels_.recent_actions_column;
  }
  if (index.column() == NameColumn) {
    const QString display_name =
        project->available
            ? project->name
            : QStringLiteral("%1 (%2)")
                  .arg(project->name, labels_.unavailable_project);
    return QStringLiteral("%1\n%2")
        .arg(display_name, project->manifest_path);
  }
  if (index.column() == LastModifiedColumn) {
    return format_local_timestamp(
        project->manifest_last_modified_unix_ms);
  }
  return format_manifest_size(project->manifest_size_bytes);
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
  if (section == LastModifiedColumn) {
    return labels_.recent_last_modified_column;
  }
  if (section == SizeColumn) {
    return labels_.recent_size_column;
  }
  if (section == ActionsColumn) {
    return labels_.recent_actions_column;
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
  emit headerDataChanged(Qt::Horizontal, NameColumn, ActionsColumn);
  if (!projects_.empty()) {
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(projects_.size()) - 1,
                           ColumnCount - 1),
                     {Qt::DisplayRole, Qt::AccessibleTextRole,
                      Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
  }
}

bool QtRecentProjectsModel::set_projects(
    std::vector<QtRecentProjectItem> projects) {
  if (projects.size() > kQtProjectHomeMaximumRecentProjects) {
    projects.resize(kQtProjectHomeMaximumRecentProjects);
  }
  if (projects_ == projects) {
    return false;
  }
  beginResetModel();
  projects_ = std::move(projects);
  endResetModel();
  return true;
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

  auto *root_layout = new QHBoxLayout(this);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  left_rail_ = new QFrame(this);
  left_rail_->setObjectName(QStringLiteral("projectHome.leftRail"));
  left_rail_->setFrameShape(QFrame::StyledPanel);
  left_rail_->setFixedWidth(248);
  left_rail_->setBackgroundRole(QPalette::AlternateBase);
  left_rail_->setAutoFillBackground(true);
  rail_layout_ = new QVBoxLayout(left_rail_);
  rail_layout_->setContentsMargins(28, 32, 28, 28);
  rail_layout_->setSpacing(8);

  brand_layout_ = new QHBoxLayout();
  brand_layout_->setSpacing(12);
  brand_icon_ = new QLabel(left_rail_);
  brand_icon_->setObjectName(QStringLiteral("projectHome.brand.icon"));
  brand_icon_->setFixedSize(44, 44);
  brand_icon_->setAlignment(Qt::AlignCenter);
  brand_icon_->setAccessibleName(QStringLiteral("Quader"));
  brand_icon_->installEventFilter(this);
  const QIcon application_icon = QApplication::windowIcon();
  if (!application_icon.isNull()) {
    brand_icon_->setPixmap(application_icon.pixmap(40, 40));
  }
  brand_layout_->addWidget(brand_icon_);

  brand_name_ = new QLabel(QStringLiteral("QUADER"), left_rail_);
  brand_name_->setObjectName(QStringLiteral("projectHome.brand.name"));
  brand_font_baseline_ = brand_name_->font();
  brand_font_baseline_.setBold(true);
  brand_font_baseline_.setPointSize(15);
  brand_name_->setFont(brand_font_baseline_);
  brand_layout_->addWidget(brand_name_, 1);
  rail_layout_->addLayout(brand_layout_);
  brand_actions_spacer_ =
      new QSpacerItem(0, 28, QSizePolicy::Minimum, QSizePolicy::Fixed);
  rail_layout_->addSpacerItem(brand_actions_spacer_);

  new_project_button_ = new QPushButton(left_rail_);
  new_project_button_->setObjectName(
      QStringLiteral("projectHome.newProject"));
  new_project_button_->setDefault(true);
  new_project_button_->setMinimumHeight(32);
  rail_layout_->addWidget(new_project_button_);

  open_project_button_ = new QPushButton(left_rail_);
  open_project_button_->setObjectName(
      QStringLiteral("projectHome.openProject"));
  open_project_button_->setMinimumHeight(32);
  rail_layout_->addWidget(open_project_button_);
  project_recent_spacer_ =
      new QSpacerItem(0, 24, QSizePolicy::Minimum, QSizePolicy::Fixed);
  rail_layout_->addSpacerItem(project_recent_spacer_);

  recent_navigation_button_ = new QPushButton(left_rail_);
  recent_navigation_button_->setObjectName(
      QStringLiteral("projectHome.navigation.recent"));
  recent_navigation_button_->setCheckable(true);
  recent_navigation_button_->setAutoExclusive(true);
  recent_navigation_button_->setChecked(true);
  recent_navigation_button_->setFlat(true);
  recent_navigation_button_->setMinimumHeight(32);
  rail_layout_->addWidget(recent_navigation_button_);
  rail_layout_->addStretch(1);
  root_layout->addWidget(left_rail_);

  auto *recent_content = new QWidget(this);
  recent_content->setObjectName(QStringLiteral("projectHome.recentContent"));
  recent_layout_ = new QVBoxLayout(recent_content);
  recent_layout_->setContentsMargins(48, 44, 48, 40);
  recent_layout_->setSpacing(14);

  recent_projects_label_ = new QLabel(recent_content);
  recent_projects_label_->setObjectName(
      QStringLiteral("projectHome.recentProjects.label"));
  recent_heading_font_baseline_ = recent_projects_label_->font();
  recent_heading_font_baseline_.setPointSize(16);
  recent_projects_label_->setFont(recent_heading_font_baseline_);
  recent_layout_->addWidget(recent_projects_label_);

  recent_projects_stack_ = new QStackedWidget(recent_content);
  recent_projects_stack_->setObjectName(
      QStringLiteral("projectHome.recentProjects.stack"));

  recent_projects_view_ = new QTableView(recent_projects_stack_);
  recent_projects_view_->setObjectName(
      QStringLiteral("projectHome.recentProjects.view"));
  recent_projects_view_->setSelectionBehavior(
      QAbstractItemView::SelectRows);
  recent_projects_view_->setSelectionMode(
      QAbstractItemView::SingleSelection);
  recent_projects_view_->setShowGrid(false);
  recent_projects_view_->setWordWrap(true);
  recent_projects_view_->setAlternatingRowColors(true);
  recent_projects_view_->verticalHeader()->hide();
  recent_projects_view_->verticalHeader()->setDefaultSectionSize(48);
  recent_projects_view_->horizontalHeader()->setStretchLastSection(false);

  recent_projects_model_ =
      new QtRecentProjectsModel(recent_projects_view_);
  recent_projects_model_->setObjectName(
      QStringLiteral("projectHome.recentProjects.model"));
  recent_projects_view_->setModel(recent_projects_model_);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::NameColumn, QHeaderView::Stretch);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::LastModifiedColumn, QHeaderView::Fixed);
  recent_projects_view_->setColumnWidth(
      QtRecentProjectsModel::LastModifiedColumn, 184);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::SizeColumn, QHeaderView::Fixed);
  recent_projects_view_->setColumnWidth(
      QtRecentProjectsModel::SizeColumn, 112);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::ActionsColumn, QHeaderView::Fixed);
  recent_projects_view_->setColumnWidth(
      QtRecentProjectsModel::ActionsColumn, 72);
  recent_projects_stack_->addWidget(recent_projects_view_);

  no_recent_projects_label_ = new QLabel(recent_projects_stack_);
  no_recent_projects_label_->setObjectName(
      QStringLiteral("projectHome.recentProjects.empty"));
  no_recent_projects_label_->setAlignment(Qt::AlignCenter);
  no_recent_projects_label_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Expanding);
  recent_projects_stack_->addWidget(no_recent_projects_label_);
  recent_layout_->addWidget(recent_projects_stack_, 1);
  root_layout->addWidget(recent_content, 1);

  recent_project_actions_menu_ = new QMenu(this);
  recent_project_actions_menu_->setObjectName(
      QStringLiteral("projectHome.recentProjects.actionsMenu"));
  open_recent_project_action_ =
      recent_project_actions_menu_->addAction(QString{});
  open_recent_project_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.open"));
  open_recent_project_location_action_ =
      recent_project_actions_menu_->addAction(QString{});
  open_recent_project_location_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.openFileLocation"));
  copy_recent_project_path_action_ =
      recent_project_actions_menu_->addAction(QString{});
  copy_recent_project_path_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.copyPath"));
  remove_recent_project_action_ =
      recent_project_actions_menu_->addAction(QString{});
  remove_recent_project_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.removeFromRecent"));
  open_recent_project_action_->setEnabled(false);
  open_recent_project_location_action_->setEnabled(false);
  copy_recent_project_path_action_->setEnabled(false);
  remove_recent_project_action_->setEnabled(false);

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
  QObject::connect(recent_projects_view_, &QTableView::clicked, this,
                   [this](const QModelIndex &index) {
                     if (index.column() ==
                         QtRecentProjectsModel::ActionsColumn) {
                       show_recent_project_actions(index);
                     }
                   });
  QObject::connect(recent_projects_view_, &QTableView::activated, this,
                   [this](const QModelIndex &index) {
                     if (index.column() ==
                         QtRecentProjectsModel::ActionsColumn) {
                       show_recent_project_actions(index);
                     } else {
                       dispatch_recent_project_open(index);
                     }
                   });
  QObject::connect(open_recent_project_action_, &QAction::triggered, this,
                   [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::Open);
                   });
  QObject::connect(open_recent_project_location_action_,
                   &QAction::triggered, this, [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::OpenFileLocation);
                   });
  QObject::connect(copy_recent_project_path_action_, &QAction::triggered,
                   this, [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::CopyPath);
                   });
  QObject::connect(remove_recent_project_action_, &QAction::triggered, this,
                   [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::RemoveFromRecent);
                   });

  apply_ui_scale(QtUiScaleMetrics{});
  refresh_labels();
  refresh_recent_projects_page();
}

void QtProjectHomePage::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (applied_ui_scale_revision_ == metrics.revision() &&
      applied_ui_scale_factor_ == metrics.factor()) {
    return;
  }
  applied_ui_scale_revision_ = metrics.revision();
  applied_ui_scale_factor_ = metrics.factor();

  left_rail_->setFixedWidth(metrics.px(248));
  rail_layout_->setContentsMargins(metrics.margins(QMargins(28, 32, 28, 28)));
  rail_layout_->setSpacing(metrics.px(8));
  brand_layout_->setSpacing(metrics.px(12));
  brand_icon_->setFixedSize(metrics.size(QSize(44, 44)));
  refresh_brand_icon();
  brand_name_->setFont(metrics.font(brand_font_baseline_));
  brand_actions_spacer_->changeSize(
      0, metrics.px(28), QSizePolicy::Minimum, QSizePolicy::Fixed);
  new_project_button_->setMinimumHeight(metrics.px(32));
  open_project_button_->setMinimumHeight(metrics.px(32));
  project_recent_spacer_->changeSize(
      0, metrics.px(24), QSizePolicy::Minimum, QSizePolicy::Fixed);
  recent_navigation_button_->setMinimumHeight(metrics.px(32));
  rail_layout_->invalidate();

  recent_layout_->setContentsMargins(
      metrics.margins(QMargins(48, 44, 48, 40)));
  recent_layout_->setSpacing(metrics.px(14));
  recent_projects_label_->setFont(
      metrics.font(recent_heading_font_baseline_));
  recent_projects_view_->verticalHeader()->setDefaultSectionSize(
      metrics.px(48));
  recent_projects_view_->setColumnWidth(
      QtRecentProjectsModel::LastModifiedColumn, metrics.px(184));
  recent_projects_view_->setColumnWidth(QtRecentProjectsModel::SizeColumn,
                                        metrics.px(112));
  recent_projects_view_->setColumnWidth(QtRecentProjectsModel::ActionsColumn,
                                        metrics.px(72));
  updateGeometry();
}

bool QtProjectHomePage::eventFilter(QObject *watched, QEvent *event) {
  if (watched == brand_icon_ && event != nullptr &&
      (event->type() == QEvent::Show ||
       event->type() == QEvent::DevicePixelRatioChange ||
       event->type() == QEvent::ScreenChangeInternal)) {
    refresh_brand_icon();
  }
  return QWidget::eventFilter(watched, event);
}

void QtProjectHomePage::refresh_brand_icon() {
  const QIcon application_icon = QApplication::windowIcon();
  if (application_icon.isNull()) {
    brand_icon_->clear();
    return;
  }
  const QtUiScaleMetrics metrics(applied_ui_scale_factor_,
                                 applied_ui_scale_revision_);
  brand_icon_->setPixmap(application_icon.pixmap(
      metrics.size(QSize(40, 40)), brand_icon_->devicePixelRatioF()));
}

void QtProjectHomePage::set_labels(QtProjectHomeLabels labels) {
  labels_ = std::move(labels);
  recent_projects_model_->set_labels(labels_);
  refresh_labels();
}

void QtProjectHomePage::set_recent_projects(
    std::vector<QtRecentProjectItem> projects) {
  if (!recent_projects_model_->set_projects(std::move(projects))) {
    return;
  }
  recent_project_actions_menu_->hide();
  recent_action_manifest_path_.clear();
  open_recent_project_action_->setEnabled(false);
  open_recent_project_location_action_->setEnabled(false);
  copy_recent_project_path_action_->setEnabled(false);
  remove_recent_project_action_->setEnabled(false);
  refresh_recent_projects_page();
}

void QtProjectHomePage::set_new_project_requested_callback(
    RequestCallback callback) {
  new_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_open_project_requested_callback(
    RequestCallback callback) {
  open_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_recent_project_action_requested_callback(
    RecentProjectActionCallback callback) {
  recent_project_action_requested_ = std::move(callback);
}

void QtProjectHomePage::dispatch_recent_project_open(
    const QModelIndex &index) {
  const QtRecentProjectItem *project =
      recent_projects_model_->project_at(index.row());
  if (project == nullptr || !project->available ||
      !recent_project_action_requested_) {
    return;
  }
  recent_project_action_requested_(QtRecentProjectAction::Open,
                                   project->manifest_path);
}

void QtProjectHomePage::dispatch_recent_project_action(
    QtRecentProjectAction action) {
  if (recent_action_manifest_path_.isEmpty() ||
      !recent_project_action_requested_) {
    return;
  }
  if (action == QtRecentProjectAction::Open &&
      !open_recent_project_action_->isEnabled()) {
    return;
  }
  recent_project_action_requested_(action, recent_action_manifest_path_);
}

void QtProjectHomePage::show_recent_project_actions(
    const QModelIndex &index) {
  const QtRecentProjectItem *project =
      recent_projects_model_->project_at(index.row());
  if (project == nullptr || !index.isValid()) {
    return;
  }
  recent_action_manifest_path_ = project->manifest_path;
  open_recent_project_action_->setEnabled(project->available);
  open_recent_project_location_action_->setEnabled(true);
  copy_recent_project_path_action_->setEnabled(true);
  remove_recent_project_action_->setEnabled(true);

  recent_projects_view_->selectRow(index.row());
  const QModelIndex action_index = recent_projects_model_->index(
      index.row(), QtRecentProjectsModel::ActionsColumn);
  recent_projects_view_->setCurrentIndex(action_index);
  const QRect action_rect = recent_projects_view_->visualRect(action_index);
  recent_project_actions_menu_->popup(
      recent_projects_view_->viewport()->mapToGlobal(action_rect.bottomRight()));
}

void QtProjectHomePage::refresh_labels() {
  new_project_button_->setText(labels_.new_project);
  new_project_button_->setAccessibleName(labels_.new_project);
  open_project_button_->setText(labels_.open_project);
  open_project_button_->setAccessibleName(labels_.open_project);
  recent_navigation_button_->setText(labels_.recent_projects);
  recent_navigation_button_->setAccessibleName(labels_.recent_projects);
  recent_projects_label_->setText(labels_.recent_projects);
  recent_projects_label_->setAccessibleName(labels_.recent_projects);
  no_recent_projects_label_->setText(labels_.no_recent_projects);
  open_recent_project_action_->setText(labels_.open_recent_project);
  open_recent_project_location_action_->setText(
      labels_.open_recent_project_location);
  copy_recent_project_path_action_->setText(
      labels_.copy_recent_project_path);
  remove_recent_project_action_->setText(labels_.remove_recent_project);
}

void QtProjectHomePage::refresh_recent_projects_page() {
  const bool has_recent_projects =
      recent_projects_model_->rowCount() != 0;
  recent_projects_stack_->setCurrentWidget(
      has_recent_projects ? static_cast<QWidget *>(recent_projects_view_)
                          : static_cast<QWidget *>(no_recent_projects_label_));
}

} // namespace quader
