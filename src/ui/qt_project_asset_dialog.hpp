////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_asset_browser.hpp"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

class QDialogButtonBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace quader {

class QtStatusMessage;

/** Selects an existing scene or an authored destination within one project. */
enum class QtProjectAssetDialogMode : std::uint8_t {
  OpenScene,
  SaveScene,
  SaveMaterial,
};

/** Localized text for the rooted asset picker and its shared browser. */
struct QtProjectAssetDialogLabels final {
  QString open_title = QStringLiteral("Open Scene");
  QString save_title = QStringLiteral("Save Scene");
  QString save_material_title = QStringLiteral("Save Material");
  QString name = QStringLiteral("Name");
  QString open = QStringLiteral("Open");
  QString save = QStringLiteral("Save");
  QString cancel = QStringLiteral("Cancel");
  QString invalid_path =
      QStringLiteral("Choose a scene location inside this project.");
  QString invalid_material_path =
      QStringLiteral("Choose a material location inside this project.");
  QtAssetBrowserLabels browser;
};

/**
 * A project-confined asset picker. It presents an immutable catalog and emits
 * folder-creation intent; the project owner remains responsible for mutations
 * and must validate the selected path again immediately before saving/opening.
 */
class QtProjectAssetDialog final : public QDialog {
public:
  using CreateFolderCallback =
      std::function<void(const QtAssetBrowserActionRequest &)>;

  QtProjectAssetDialog(QtProjectAssetDialogMode mode, QString project_root,
                       QtAssetBrowserSnapshot snapshot,
                       QWidget *parent = nullptr);
  ~QtProjectAssetDialog() override;

  [[nodiscard]] QtAssetBrowser &browser() const;
  /** Empty until acceptance, and always portable relative to the project. */
  [[nodiscard]] QString selected_relative_path() const;
  void set_initial_path(QString relative_path);
  void set_labels(QtProjectAssetDialogLabels labels);
  void set_snapshot(QtAssetBrowserSnapshot snapshot);
  void set_create_folder_callback(CreateFolderCallback callback);
  /** Prevents Save Copy from targeting an asset/folder pending deletion. */
  void set_excluded_paths(QStringList relative_paths);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  void accept() override;
  void reject() override;

private:
  [[nodiscard]] QString candidate_relative_path() const;
  [[nodiscard]] bool allowed_folder(const QString &folder) const;
  [[nodiscard]] bool excluded(const QString &path) const;
  void selection_changed();
  void refresh_validation();
  void perform_browser_action(const QtAssetBrowserActionRequest &request);

  QtProjectAssetDialogMode mode_;
  QString project_root_;
  QString selected_relative_path_;
  QStringList excluded_paths_;
  QtProjectAssetDialogLabels labels_;
  CreateFolderCallback create_folder_;
  QtAssetBrowser *browser_ = nullptr;
  QLabel *name_label_ = nullptr;
  QLineEdit *name_ = nullptr;
  QtStatusMessage *error_ = nullptr;
  QDialogButtonBox *buttons_ = nullptr;
  QPushButton *accept_button_ = nullptr;
  QVBoxLayout *root_layout_ = nullptr;
  QHBoxLayout *name_layout_ = nullptr;
  QtUiScaleMetrics ui_scale_metrics_;
};

} // namespace quader
