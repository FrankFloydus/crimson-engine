////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_find_replace_dialog.hpp"

#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace quader {
namespace {

constexpr int kPreviewSize = 128;

QString column_object_prefix(QtMaterialFindReplaceColumn column) {
  return column == QtMaterialFindReplaceColumn::Find
             ? QStringLiteral("materialFindReplace.find")
             : QStringLiteral("materialFindReplace.replace");
}

} // namespace

QtMaterialFindReplaceDialog::QtMaterialFindReplaceDialog(QWidget *parent)
    : QDialog(parent, Qt::Dialog) {
  setObjectName(QStringLiteral("materialFindReplace.dialog"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowModality(Qt::WindowModal);
  setWindowFlag(Qt::WindowMaximizeButtonHint, false);
  resize(380, 300);

  auto *root_layout = new QVBoxLayout(this);
  auto *columns_layout = new QHBoxLayout();
  columns_layout->setSpacing(28);
  root_layout->addLayout(columns_layout, 1);

  const auto build_column = [this, columns_layout](
                                QtMaterialFindReplaceColumn column,
                                ColumnWidgets &result) {
    auto *column_layout = new QVBoxLayout();
    column_layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    const QString prefix = column_object_prefix(column);

    result.heading = new QLabel(this);
    result.heading->setObjectName(prefix + QStringLiteral(".heading"));
    result.heading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    column_layout->addWidget(result.heading);

    result.preview = new QLabel(this);
    result.preview->setObjectName(prefix + QStringLiteral(".preview"));
    result.preview->setFixedSize(kPreviewSize, kPreviewSize);
    result.preview->setAlignment(Qt::AlignCenter);
    result.preview->setFrameShape(QFrame::StyledPanel);
    result.preview->setFrameShadow(QFrame::Sunken);
    result.preview->setScaledContents(false);
    result.preview->setWordWrap(true);
    column_layout->addWidget(result.preview, 0, Qt::AlignHCenter);

    result.name = new QLabel(this);
    result.name->setObjectName(prefix + QStringLiteral(".name"));
    result.name->setFixedWidth(kPreviewSize);
    result.name->setAlignment(Qt::AlignCenter);
    column_layout->addWidget(result.name, 0, Qt::AlignHCenter);

    result.browse = new QPushButton(this);
    result.browse->setObjectName(prefix + QStringLiteral(".browse"));
    column_layout->addWidget(result.browse, 0, Qt::AlignHCenter);
    QObject::connect(result.browse, &QPushButton::clicked, this,
                     [this, column]() { browse(column); });

    columns_layout->addLayout(column_layout, 1);
  };
  build_column(QtMaterialFindReplaceColumn::Find, find_widgets_);
  build_column(QtMaterialFindReplaceColumn::Replace, replace_widgets_);

  auto *actions_layout = new QHBoxLayout();
  actions_layout->addStretch(1);
  replace_button_ = new QPushButton(this);
  replace_button_->setObjectName(
      QStringLiteral("materialFindReplace.action.replace"));
  replace_button_->setDefault(true);
  replace_button_->setEnabled(false);
  actions_layout->addWidget(replace_button_);
  cancel_button_ = new QPushButton(this);
  cancel_button_->setObjectName(
      QStringLiteral("materialFindReplace.action.cancel"));
  actions_layout->addWidget(cancel_button_);
  root_layout->addLayout(actions_layout);

  QObject::connect(replace_button_, &QPushButton::clicked, this,
                   [this]() { perform_replace(); });
  QObject::connect(cancel_button_, &QPushButton::clicked, this,
                   [this]() { reject(); });

  set_labels(labels_);
  clear_materials();
}

QtMaterialFindReplaceDialog::~QtMaterialFindReplaceDialog() = default;

void QtMaterialFindReplaceDialog::set_labels(
    QtMaterialFindReplaceLabels labels) {
  labels_ = std::move(labels);
  setWindowTitle(labels_.window_title);
  find_widgets_.heading->setText(labels_.find);
  replace_widgets_.heading->setText(labels_.replace);
  replace_button_->setText(labels_.replace_action);
  replace_button_->setAccessibleName(labels_.replace_action);
  cancel_button_->setText(labels_.cancel_action);
  cancel_button_->setAccessibleName(labels_.cancel_action);
  for (const QtMaterialFindReplaceColumn column :
       {QtMaterialFindReplaceColumn::Find,
        QtMaterialFindReplaceColumn::Replace}) {
    ColumnWidgets &column_widgets = widgets(column);
    const QString heading = column_widgets.heading->text();
    column_widgets.browse->setText(labels_.browse);
    column_widgets.browse->setAccessibleName(
        heading + QStringLiteral(": ") + labels_.browse);
    refresh_column(column);
  }
}

void QtMaterialFindReplaceDialog::set_material(
    QtMaterialFindReplaceColumn column,
    std::optional<QtMaterialPresentationItem> material) {
  if (column == QtMaterialFindReplaceColumn::Find) {
    if (find_material_ == material) {
      return;
    }
    find_material_ = std::move(material);
  } else {
    if (replacement_material_ == material) {
      return;
    }
    replacement_material_ = std::move(material);
  }
  refresh_column(column);
  refresh_validation();
}

void QtMaterialFindReplaceDialog::clear_materials() {
  find_material_.reset();
  replacement_material_.reset();
  refresh_column(QtMaterialFindReplaceColumn::Find);
  refresh_column(QtMaterialFindReplaceColumn::Replace);
  refresh_validation();
}

const std::optional<QtMaterialPresentationItem> &
QtMaterialFindReplaceDialog::material(
    QtMaterialFindReplaceColumn column) const {
  return column == QtMaterialFindReplaceColumn::Find
             ? find_material_
             : replacement_material_;
}

bool QtMaterialFindReplaceDialog::replace_available() const {
  return find_material_.has_value() && find_material_->valid() &&
         replacement_material_.has_value() &&
         replacement_material_->replacement_valid() &&
         find_material_->reference != replacement_material_->reference;
}

void QtMaterialFindReplaceDialog::set_browse_material_callback(
    BrowseMaterialCallback callback) {
  browse_material_ = std::move(callback);
}

void QtMaterialFindReplaceDialog::set_replace_materials_callback(
    ReplaceMaterialsCallback callback) {
  replace_materials_ = std::move(callback);
}

QtMaterialFindReplaceDialog::ColumnWidgets &
QtMaterialFindReplaceDialog::widgets(QtMaterialFindReplaceColumn column) {
  return column == QtMaterialFindReplaceColumn::Find ? find_widgets_
                                                     : replace_widgets_;
}

const QtMaterialFindReplaceDialog::ColumnWidgets &
QtMaterialFindReplaceDialog::widgets(
    QtMaterialFindReplaceColumn column) const {
  return column == QtMaterialFindReplaceColumn::Find ? find_widgets_
                                                     : replace_widgets_;
}

void QtMaterialFindReplaceDialog::refresh_column(
    QtMaterialFindReplaceColumn column) {
  ColumnWidgets &column_widgets = widgets(column);
  const std::optional<QtMaterialPresentationItem> &column_material =
      material(column);
  if (!column_material.has_value() || !column_material->valid()) {
    column_widgets.preview->setPixmap(QPixmap{});
    column_widgets.preview->setText(labels_.empty);
    column_widgets.preview->setAccessibleName(
        column_widgets.heading->text() + QStringLiteral(": ") +
        labels_.empty);
    column_widgets.name->clear();
    column_widgets.name->setToolTip(QString{});
    return;
  }

  column_widgets.preview->setText(QString{});
  column_widgets.preview->setPixmap(qt_material_preview_pixmap(
      *column_material, QSize(kPreviewSize, kPreviewSize),
      column_widgets.preview->devicePixelRatioF()));
  column_widgets.preview->setAccessibleName(
      column_widgets.heading->text() + QStringLiteral(": ") +
      column_material->name);
  const QFontMetrics metrics(column_widgets.name->font());
  column_widgets.name->setText(
      metrics.elidedText(column_material->name, Qt::ElideRight,
                         column_widgets.name->width()));
  column_widgets.name->setToolTip(column_material->name);
}

void QtMaterialFindReplaceDialog::refresh_validation() {
  replace_button_->setEnabled(replace_available());
}

void QtMaterialFindReplaceDialog::browse(
    QtMaterialFindReplaceColumn column) {
  if (browse_material_) {
    browse_material_(column);
  }
}

void QtMaterialFindReplaceDialog::perform_replace() {
  if (!replace_available() || !replace_materials_) {
    return;
  }
  if (replace_materials_(find_material_->reference,
                         replacement_material_->reference)) {
    accept();
  }
}

} // namespace quader
