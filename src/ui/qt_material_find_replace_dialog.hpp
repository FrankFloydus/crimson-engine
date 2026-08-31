////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_material_presentation.hpp"

#include <QDialog>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

class QLabel;
class QPushButton;

namespace quader {

/** Identifies the source or replacement side of the material dialog. */
enum class QtMaterialFindReplaceColumn : std::uint8_t {
  Find,
  Replace,
};

/** Carries localizable text for the material find-and-replace dialog. */
struct QtMaterialFindReplaceLabels final {
  QString window_title = QStringLiteral("Find/Replace Materials");
  QString find = QStringLiteral("Find");
  QString replace = QStringLiteral("Replace");
  QString browse = QStringLiteral("Browse…");
  QString empty = QStringLiteral("No material selected");
  QString replace_action = QStringLiteral("Replace");
  QString cancel_action = QStringLiteral("Cancel");
};

/**
 * Retained, window-modal material-reference replacement presentation. Domain
 * state is supplied as immutable items and all behavior leaves through typed
 * semantic callbacks.
 */
class QtMaterialFindReplaceDialog final : public QDialog {
public:
  using BrowseMaterialCallback =
      std::function<void(QtMaterialFindReplaceColumn)>;
  using ReplaceMaterialsCallback =
      std::function<bool(QuaderMaterialReference,
                         QuaderMaterialReference)>;

  explicit QtMaterialFindReplaceDialog(QWidget *parent = nullptr);
  ~QtMaterialFindReplaceDialog() override;

  QtMaterialFindReplaceDialog(const QtMaterialFindReplaceDialog &) = delete;
  QtMaterialFindReplaceDialog &
  operator=(const QtMaterialFindReplaceDialog &) = delete;

  void set_labels(QtMaterialFindReplaceLabels labels);
  void set_material(QtMaterialFindReplaceColumn column,
                    std::optional<QtMaterialPresentationItem> material);
  void set_find_material(
      std::optional<QtMaterialPresentationItem> material) {
    set_material(QtMaterialFindReplaceColumn::Find, std::move(material));
  }
  void set_replacement_material(
      std::optional<QtMaterialPresentationItem> material) {
    set_material(QtMaterialFindReplaceColumn::Replace,
                 std::move(material));
  }
  void clear_materials();

  [[nodiscard]] const std::optional<QtMaterialPresentationItem> &
  material(QtMaterialFindReplaceColumn column) const;
  [[nodiscard]] bool replace_available() const;

  void set_browse_material_callback(BrowseMaterialCallback callback);
  void set_replace_materials_callback(ReplaceMaterialsCallback callback);

private:
  /** Groups widgets belonging to one material-reference column. */
  struct ColumnWidgets final {
    QLabel *heading = nullptr;
    QLabel *preview = nullptr;
    QLabel *name = nullptr;
    QPushButton *browse = nullptr;
  };

  [[nodiscard]] ColumnWidgets &widgets(QtMaterialFindReplaceColumn column);
  [[nodiscard]] const ColumnWidgets &
  widgets(QtMaterialFindReplaceColumn column) const;
  void refresh_column(QtMaterialFindReplaceColumn column);
  void refresh_validation();
  void browse(QtMaterialFindReplaceColumn column);
  void perform_replace();

  ColumnWidgets find_widgets_;
  ColumnWidgets replace_widgets_;
  std::optional<QtMaterialPresentationItem> find_material_;
  std::optional<QtMaterialPresentationItem> replacement_material_;
  QPushButton *replace_button_ = nullptr;
  QPushButton *cancel_button_ = nullptr;
  QtMaterialFindReplaceLabels labels_;
  BrowseMaterialCallback browse_material_;
  ReplaceMaterialsCallback replace_materials_;
};

} // namespace quader
