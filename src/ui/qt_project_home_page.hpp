////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QAbstractTableModel>
#include <QFont>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

class QLabel;
class QAction;
class QEvent;
class QFrame;
class QHBoxLayout;
class QMenu;
class QPushButton;
class QSpacerItem;
class QStackedWidget;
class QTableView;
class QVBoxLayout;

namespace quader {

inline constexpr int kQtProjectHomeMaximumRecentProjects = 10;

/**
 * Read-only presentation data for one project in the Home page's recent list.
 * The project owner remains responsible for canonicalization and availability.
 */
struct QtRecentProjectItem {
  QString name;
  QString manifest_path;
  bool available = true;
  std::optional<std::uint64_t> manifest_last_modified_unix_ms;
  std::optional<std::uintmax_t> manifest_size_bytes;

  bool operator==(const QtRecentProjectItem &) const = default;
};

/**
 * User-facing Home page text supplied by the shell's localization boundary.
 */
struct QtProjectHomeLabels {
  QString new_project;
  QString open_project;
  QString recent_projects;
  QString recent_name_column;
  QString recent_last_modified_column;
  QString recent_size_column;
  QString recent_actions_column;
  QString no_recent_projects;
  QString unavailable_project;
  QString open_recent_project;
  QString open_recent_project_location;
  QString copy_recent_project_path;
  QString remove_recent_project;

  [[nodiscard]] static QtProjectHomeLabels defaults();
};

/**
 * Read-only table projection for recent projects. It intentionally contains no
 * storage, canonicalization, or project-opening behavior.
 */
class QtRecentProjectsModel final : public QAbstractTableModel {
public:
  /// Defines the stable columns presented by the recent-project table.
  enum Column {
    NameColumn = 0,
    LastModifiedColumn,
    SizeColumn,
    ActionsColumn,
    ColumnCount,
  };

  /// Defines model roles used to activate and style recent-project rows.
  enum Role {
    ManifestPathRole = Qt::UserRole + 1,
    AvailableRole,
  };

  explicit QtRecentProjectsModel(QObject *parent = nullptr);

  [[nodiscard]] int
  rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int
  columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(
      int section, Qt::Orientation orientation,
      int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

  void set_labels(QtProjectHomeLabels labels);
  [[nodiscard]] bool
  set_projects(std::vector<QtRecentProjectItem> projects);
  [[nodiscard]] const std::vector<QtRecentProjectItem> &projects() const {
    return projects_;
  }
  [[nodiscard]] const QtRecentProjectItem *project_at(int row) const;

private:
  QtProjectHomeLabels labels_ = QtProjectHomeLabels::defaults();
  std::vector<QtRecentProjectItem> projects_;
};

/**
 * User intent emitted by the shared recent-project actions menu.
 */
enum class QtRecentProjectAction {
  Open,
  OpenFileLocation,
  CopyPath,
  RemoveFromRecent,
};

/**
 * Stock-Qt project onboarding page. Buttons and recent-row activation dispatch
 * intent through callbacks; project lifecycle and recent storage stay outside
 * this presentation component.
 */
class QtProjectHomePage final : public QWidget {
public:
  using RequestCallback = std::function<void()>;
  using RecentProjectActionCallback =
      std::function<void(QtRecentProjectAction, QString)>;

  explicit QtProjectHomePage(QWidget *parent = nullptr);

  /** Reflows retained Home chrome from immutable 100% design metrics. */
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  void set_labels(QtProjectHomeLabels labels);
  void set_recent_projects(std::vector<QtRecentProjectItem> projects);

  void set_new_project_requested_callback(RequestCallback callback);
  void set_open_project_requested_callback(RequestCallback callback);
  void set_recent_project_action_requested_callback(
      RecentProjectActionCallback callback);

  [[nodiscard]] const QtRecentProjectsModel &recent_projects_model() const {
    return *recent_projects_model_;
  }
  [[nodiscard]] QtRecentProjectsModel &recent_projects_model() {
    return *recent_projects_model_;
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void dispatch_recent_project_open(const QModelIndex &index);
  void dispatch_recent_project_action(QtRecentProjectAction action);
  void show_recent_project_actions(const QModelIndex &index);
  void refresh_brand_icon();
  void refresh_labels();
  void refresh_recent_projects_page();

  QtProjectHomeLabels labels_ = QtProjectHomeLabels::defaults();
  QFrame *left_rail_ = nullptr;
  QVBoxLayout *rail_layout_ = nullptr;
  QHBoxLayout *brand_layout_ = nullptr;
  QSpacerItem *brand_actions_spacer_ = nullptr;
  QSpacerItem *project_recent_spacer_ = nullptr;
  QLabel *brand_icon_ = nullptr;
  QLabel *brand_name_ = nullptr;
  QFont brand_font_baseline_;
  QPushButton *new_project_button_ = nullptr;
  QPushButton *open_project_button_ = nullptr;
  QPushButton *recent_navigation_button_ = nullptr;
  QLabel *recent_projects_label_ = nullptr;
  QFont recent_heading_font_baseline_;
  QVBoxLayout *recent_layout_ = nullptr;
  QStackedWidget *recent_projects_stack_ = nullptr;
  QTableView *recent_projects_view_ = nullptr;
  QLabel *no_recent_projects_label_ = nullptr;
  QtRecentProjectsModel *recent_projects_model_ = nullptr;
  QMenu *recent_project_actions_menu_ = nullptr;
  QAction *open_recent_project_action_ = nullptr;
  QAction *open_recent_project_location_action_ = nullptr;
  QAction *copy_recent_project_path_action_ = nullptr;
  QAction *remove_recent_project_action_ = nullptr;
  QString recent_action_manifest_path_;
  RequestCallback new_project_requested_;
  RequestCallback open_project_requested_;
  RecentProjectActionCallback recent_project_action_requested_;
  float applied_ui_scale_factor_ = 0.0F;
  std::uint64_t applied_ui_scale_revision_ =
      std::numeric_limits<std::uint64_t>::max();
};

} // namespace quader
