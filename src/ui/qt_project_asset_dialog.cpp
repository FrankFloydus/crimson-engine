////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_project_asset_dialog.hpp"

#include "io/project/project_file_system.hpp"
#include "ui/qt_status_message.hpp"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace quader {
namespace {

QString portable_path(QString path) {
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return path;
}

bool same_path(const QString &left, const QString &right) {
  return left.compare(right, Qt::CaseInsensitive) == 0;
}

QtAssetBrowserKind dialog_asset_kind(QtProjectAssetDialogMode mode) {
  return mode == QtProjectAssetDialogMode::SaveMaterial
             ? QtAssetBrowserKind::Material
             : QtAssetBrowserKind::Scene;
}

} // namespace

QtProjectAssetDialog::QtProjectAssetDialog(
    QtProjectAssetDialogMode mode, QString project_root,
    QtAssetBrowserSnapshot snapshot, QWidget *parent)
    : QDialog(parent, Qt::Dialog), mode_(mode),
      project_root_(std::move(project_root)) {
  setObjectName(QStringLiteral("projectAsset.dialog"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowModality(Qt::WindowModal);
  resize(800, 560);

  root_layout_ = new QVBoxLayout(this);
  root_layout_->setContentsMargins(12, 12, 12, 12);
  root_layout_->setSpacing(10);
  browser_ = new QtAssetBrowser(this);
  browser_->set_picker_mode(true);
  browser_->set_type_filter(dialog_asset_kind(mode_));
  browser_->set_snapshot(std::move(snapshot));
  root_layout_->addWidget(browser_, 1);

  name_layout_ = new QHBoxLayout();
  name_layout_->setSpacing(8);
  name_label_ = new QLabel(this);
  name_ = new QLineEdit(this);
  name_->setObjectName(QStringLiteral("projectAsset.name"));
  name_label_->setBuddy(name_);
  name_layout_->addWidget(name_label_);
  name_layout_->addWidget(name_, 1);
  root_layout_->addLayout(name_layout_);
  const bool saving = mode_ != QtProjectAssetDialogMode::OpenScene;
  name_label_->setVisible(saving);
  name_->setVisible(saving);

  error_ = new QtStatusMessage(this);
  error_->setObjectName(QStringLiteral("projectAsset.error"));
  error_->setWordWrap(true);
  error_->hide();
  root_layout_->addWidget(error_);

  buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  accept_button_ = buttons_->addButton(saving ? QDialogButtonBox::Save
                                            : QDialogButtonBox::Open);
  accept_button_->setObjectName(QStringLiteral("projectAsset.accept"));
  accept_button_->setDefault(true);
  root_layout_->addWidget(buttons_);
  connect(buttons_, &QDialogButtonBox::accepted, this,
          &QtProjectAssetDialog::accept);
  connect(buttons_, &QDialogButtonBox::rejected, this,
          &QtProjectAssetDialog::reject);
  connect(name_, &QLineEdit::textChanged, this,
          [this]() { refresh_validation(); });
  browser_->set_selection_changed_callback(
      [this]() { selection_changed(); });
  browser_->set_action_callback(
      [this](const QtAssetBrowserActionRequest &request) {
        perform_browser_action(request);
      });
  set_labels(labels_);
  refresh_validation();
}

QtProjectAssetDialog::~QtProjectAssetDialog() = default;

QtAssetBrowser &QtProjectAssetDialog::browser() const { return *browser_; }

QString QtProjectAssetDialog::selected_relative_path() const {
  return selected_relative_path_;
}

void QtProjectAssetDialog::set_initial_path(QString relative_path) {
  relative_path = portable_path(std::move(relative_path));
  if (relative_path.endsWith(QLatin1Char('/'))) {
    relative_path.chop(1);
  }
  if (allowed_folder(relative_path)) {
    browser_->set_selected_folder(relative_path);
    return;
  }
  const qsizetype separator = relative_path.lastIndexOf(QLatin1Char('/'));
  const QString folder = separator < 0 ? QString{} : relative_path.left(separator);
  if (!allowed_folder(folder)) {
    return;
  }
  browser_->set_selected_folder(folder);
  if (mode_ != QtProjectAssetDialogMode::OpenScene) {
    name_->setText(relative_path.mid(separator + 1));
    name_->selectAll();
    name_->setFocus(Qt::OtherFocusReason);
  }
  if (const auto found = std::find_if(
          browser_->snapshot().items.begin(), browser_->snapshot().items.end(),
          [this, &relative_path](const QtAssetBrowserItem &item) {
            return item.kind == dialog_asset_kind(mode_) &&
                   same_path(item.relative_path, relative_path);
          });
      found != browser_->snapshot().items.end()) {
    (void)browser_->reveal_item(found->id);
  }
  refresh_validation();
}

void QtProjectAssetDialog::set_labels(QtProjectAssetDialogLabels labels) {
  labels_ = std::move(labels);
  const bool saving = mode_ != QtProjectAssetDialogMode::OpenScene;
  setWindowTitle(mode_ == QtProjectAssetDialogMode::SaveMaterial
                     ? labels_.save_material_title
                     : (saving ? labels_.save_title : labels_.open_title));
  name_label_->setText(labels_.name);
  name_->setAccessibleName(labels_.name);
  accept_button_->setText(saving ? labels_.save : labels_.open);
  buttons_->button(QDialogButtonBox::Cancel)->setText(labels_.cancel);
  browser_->set_labels(labels_.browser);
  if (!error_->isHidden()) {
    error_->present(mode_ == QtProjectAssetDialogMode::SaveMaterial
                        ? labels_.invalid_material_path : labels_.invalid_path,
                    true);
  }
}

void QtProjectAssetDialog::set_snapshot(QtAssetBrowserSnapshot snapshot) {
  if (snapshot.generation != browser_->snapshot().generation) {
    reject();
    return;
  }
  if (browser_->set_snapshot(std::move(snapshot))) {
    refresh_validation();
  }
}

void QtProjectAssetDialog::set_create_folder_callback(
    CreateFolderCallback callback) {
  create_folder_ = std::move(callback);
}

void QtProjectAssetDialog::set_excluded_paths(QStringList relative_paths) {
  for (QString &path : relative_paths) {
    path = portable_path(std::move(path));
    while (path.endsWith(QLatin1Char('/'))) {
      path.chop(1);
    }
  }
  excluded_paths_ = std::move(relative_paths);
  refresh_validation();
}

void QtProjectAssetDialog::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  root_layout_->setContentsMargins(metrics.margins(QMargins(12, 12, 12, 12)));
  root_layout_->setSpacing(metrics.px(10));
  name_layout_->setSpacing(metrics.px(8));
  browser_->apply_ui_scale(metrics);
  updateGeometry();
}

void QtProjectAssetDialog::accept() {
  const QString candidate = candidate_relative_path();
  bool valid = !candidate.isEmpty();
  if (valid) {
    const auto path = ProjectRelativePath::parse_portable(
        candidate.toUtf8().toStdString());
    const auto file_system = ProjectFileSystem::open(
        std::filesystem::path(project_root_.toStdWString()));
    valid = path.ok() && file_system.ok();
    if (valid) {
      valid = mode_ != QtProjectAssetDialogMode::OpenScene
                  ? file_system.value().resolve_for_write(path.value()).ok()
                  : file_system.value().resolve_existing_file(path.value()).ok();
    }
  }
  if (!valid) {
    selected_relative_path_.clear();
    error_->present(mode_ == QtProjectAssetDialogMode::SaveMaterial
                        ? labels_.invalid_material_path : labels_.invalid_path,
                    true, true);
    error_->show();
    return;
  }
  selected_relative_path_ = candidate;
  QDialog::accept();
}

void QtProjectAssetDialog::reject() {
  selected_relative_path_.clear();
  QDialog::reject();
}

QString QtProjectAssetDialog::candidate_relative_path() const {
  QString candidate;
  if (mode_ == QtProjectAssetDialogMode::OpenScene) {
    const auto selected = browser_->selected_items();
    if (selected.size() != 1 || selected.front().kind != QtAssetBrowserKind::Scene) {
      return {};
    }
    candidate = selected.front().relative_path;
  } else {
    QString name = name_->text();
    if (name.trimmed().isEmpty() || name != name.trimmed() ||
        name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
        name.endsWith(QLatin1Char('.'))) {
      return {};
    }
    const QString extension = mode_ == QtProjectAssetDialogMode::SaveMaterial
                                  ? QStringLiteral("qmat")
                                  : QStringLiteral("qdr");
    const QString suffix = QFileInfo(name).suffix();
    if (suffix.isEmpty()) {
      name += QLatin1Char('.') + extension;
    } else if (suffix.compare(extension, Qt::CaseInsensitive) != 0) {
      return {};
    }
    const QString folder = browser_->selected_folder();
    if (!allowed_folder(folder)) {
      return {};
    }
    candidate = folder.isEmpty() ? name : folder + QLatin1Char('/') + name;
  }
  if (excluded(candidate) ||
      !ProjectRelativePath::parse_portable(candidate.toUtf8().toStdString()).ok()) {
    return {};
  }
  return candidate;
}

bool QtProjectAssetDialog::allowed_folder(const QString &folder) const {
  if (folder.isEmpty()) {
    return true;
  }
  return std::any_of(browser_->snapshot().items.begin(),
                     browser_->snapshot().items.end(),
                     [&folder](const QtAssetBrowserItem &item) {
                       return item.kind == QtAssetBrowserKind::Folder &&
                              same_path(item.relative_path, folder);
                     });
}

bool QtProjectAssetDialog::excluded(const QString &path) const {
  return std::any_of(excluded_paths_.begin(), excluded_paths_.end(),
                     [&path](const QString &excluded_path) {
                       return excluded_path.isEmpty() ||
                              same_path(path, excluded_path) ||
                              path.startsWith(excluded_path + QLatin1Char('/'),
                                              Qt::CaseInsensitive);
                     });
}

void QtProjectAssetDialog::selection_changed() {
  if (mode_ != QtProjectAssetDialogMode::OpenScene) {
    const auto selected = browser_->selected_items();
    if (selected.size() == 1 && selected.front().kind == dialog_asset_kind(mode_)) {
      name_->setText(QFileInfo(selected.front().relative_path).fileName());
    }
  }
  refresh_validation();
}

void QtProjectAssetDialog::refresh_validation() {
  selected_relative_path_.clear();
  accept_button_->setEnabled(!candidate_relative_path().isEmpty());
  error_->hide();
}

void QtProjectAssetDialog::perform_browser_action(
    const QtAssetBrowserActionRequest &request) {
  if (request.generation != browser_->snapshot().generation ||
      request.dependency_revision != browser_->snapshot().dependency_revision) {
    return;
  }
  if (request.action == QtAssetBrowserAction::CreateFolder && create_folder_) {
    // Copy the callback because the owner may rebind or close this dialog.
    const CreateFolderCallback callback = create_folder_;
    callback(request);
    return;
  }
  if (request.action == QtAssetBrowserAction::Open) {
    selection_changed();
    accept();
  }
}

} // namespace quader
