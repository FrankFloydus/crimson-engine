////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <string_view>

namespace quader {

class QuaderCommandsApi;
class LocalizationService;

/**
 * Optional presentation facts layered over one public command descriptor.
 *
 * Empty category/breadcrumb values are derived from the stable command ID.
 * disabled_reason is displayed only for disabled commands. Setting user_facing
 * to false removes an otherwise public registry entry from command search.
 */
struct QtCommandPaletteMetadata final {
  QString label;
  QString category;
  QString breadcrumb;
  QStringList aliases;
  QString disabled_reason;
  bool user_facing = true;
};

/**
 * On-demand, searchable presentation of public editor commands.
 *
 * open_palette() captures commands exactly once for that opening. Typing only
 * filters the captured snapshot, so command registry enumeration and command
 * availability predicates never become frame-loop work. Intent is dispatched
 * exclusively through QuaderCommandsApi.
 */
class QtCommandPalette final : public QDialog {
public:
  using MetadataResolver =
      std::function<QtCommandPaletteMetadata(std::string_view command_id)>;
  using ExecutionObserver =
      std::function<void(std::string command_id)>;

  explicit QtCommandPalette(QuaderCommandsApi &commands,
                            LocalizationService &localization,
                            QWidget *parent = nullptr);
  ~QtCommandPalette() override;

  QtCommandPalette(const QtCommandPalette &) = delete;
  QtCommandPalette &operator=(const QtCommandPalette &) = delete;

  /** Rebinds the runtime-owned command facade after an editor replacement. */
  void rebind_commands(QuaderCommandsApi &commands) noexcept;
  /** Reapplies chrome and command labels after a locale change. */
  void refresh_localization();
  void set_metadata_resolver(MetadataResolver resolver);
  void set_execution_observer(ExecutionObserver observer);

  /** Captures current public commands and opens the reusable dialog. */
  void open_palette();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
