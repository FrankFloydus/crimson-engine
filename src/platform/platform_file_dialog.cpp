////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/platform_file_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cderr.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

namespace quader {
namespace {

#if defined(_WIN32)
constexpr wchar_t kProjectFileFilter[] =
    L"Quader Projects (*.qproj)\0*.qproj\0All Files (*.*)\0*.*\0";
constexpr wchar_t kProjectDefaultExtension[] = L"qproj";
constexpr wchar_t kSceneFileFilter[] =
    L"Quader Scenes (*.qdr)\0*.qdr\0All Files (*.*)\0*.*\0";
constexpr wchar_t kSceneDefaultExtension[] = L"qdr";
constexpr wchar_t kMaterialFileFilter[] =
    L"Quader Materials (*.qmat)\0*.qmat\0All Files (*.*)\0*.*\0";
constexpr wchar_t kMaterialDefaultExtension[] = L"qmat";
constexpr wchar_t kTextureFileFilter[] =
    L"Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff;*.tga)\0"
    L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff;*.tga\0"
    L"All Files (*.*)\0*.*\0";
constexpr wchar_t kObjFileFilter[] =
    L"Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
constexpr wchar_t kObjDefaultExtension[] = L"obj";
constexpr std::size_t kPathBufferCharacters = 32768U;

std::vector<wchar_t> path_buffer(const std::filesystem::path &initial_path,
                                 std::size_t capacity =
                                     kPathBufferCharacters) {
  const std::wstring text = initial_path.wstring();
  capacity = std::max(capacity, text.size() + 1U);
  std::vector<wchar_t> buffer(capacity, L'\0');
  if (!text.empty()) {
    const std::size_t count = std::min(text.size(), buffer.size() - 1U);
    std::copy_n(text.begin(), count, buffer.begin());
  }
  return buffer;
}

OPENFILENAMEW base_open_file_name(const PlatformFileDialogOptions &options,
                                  std::vector<wchar_t> &buffer,
                                  const std::wstring &initial_directory,
                                  const wchar_t *filter,
                                  const wchar_t *default_extension) {
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = static_cast<HWND>(options.owner_window_handle);
  dialog.lpstrFilter = filter;
  dialog.lpstrFile = buffer.data();
  dialog.nMaxFile = static_cast<DWORD>(buffer.size());
  dialog.lpstrDefExt = default_extension;
  dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
  if (!initial_directory.empty()) {
    dialog.lpstrInitialDir = initial_directory.c_str();
  }
  return dialog;
}

std::optional<std::filesystem::path>
choose_folder(const PlatformFileDialogOptions &options) {
  const HRESULT initialized =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool uninitialize = SUCCEEDED(initialized);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return std::nullopt;
  }

  IFileOpenDialog *dialog = nullptr;
  HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog));
  if (FAILED(result) || dialog == nullptr) {
    if (uninitialize) {
      CoUninitialize();
    }
    return std::nullopt;
  }

  FILEOPENDIALOGOPTIONS flags = {};
  if (SUCCEEDED(dialog->GetOptions(&flags))) {
    (void)dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                             FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
  }
  if (!options.initial_directory.empty()) {
    IShellItem *initial = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(
            options.initial_directory.c_str(), nullptr,
            IID_PPV_ARGS(&initial))) &&
        initial != nullptr) {
      (void)dialog->SetFolder(initial);
      initial->Release();
    }
  }

  std::optional<std::filesystem::path> selected;
  if (SUCCEEDED(dialog->Show(static_cast<HWND>(options.owner_window_handle)))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
      PWSTR value = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value)) &&
          value != nullptr) {
        selected = std::filesystem::path(value);
        CoTaskMemFree(value);
      }
      item->Release();
    }
  }
  dialog->Release();
  if (uninitialize) {
    CoUninitialize();
  }
  return selected;
}
#endif

} // namespace

namespace platform_file_dialog_detail {

std::optional<std::vector<std::filesystem::path>>
parse_explorer_file_dialog_selection(const bool accepted,
                                     const std::span<const wchar_t> buffer) {
  if (!accepted || buffer.empty() || buffer.front() == L'\0') {
    return std::nullopt;
  }

  const auto first_terminator =
      std::find(buffer.begin(), buffer.end(), L'\0');
  if (first_terminator == buffer.end()) {
    return std::nullopt;
  }

  const std::wstring_view first(buffer.data(),
                                static_cast<std::size_t>(
                                    first_terminator - buffer.begin()));
  const auto next = first_terminator + 1;
  if (next == buffer.end() || *next == L'\0') {
    return std::vector<std::filesystem::path>{std::filesystem::path(first)};
  }

  const std::filesystem::path directory(first);
  std::vector<std::filesystem::path> selected;
  auto segment_begin = next;
  while (segment_begin != buffer.end() && *segment_begin != L'\0') {
    const auto segment_end =
        std::find(segment_begin, buffer.end(), L'\0');
    if (segment_end == buffer.end()) {
      return std::nullopt;
    }
    selected.emplace_back(
        directory /
        std::filesystem::path(std::wstring_view(
            &*segment_begin,
            static_cast<std::size_t>(segment_end - segment_begin))));
    segment_begin = segment_end + 1;
  }
  if (selected.empty()) {
    return std::nullopt;
  }
  return selected;
}

std::optional<std::size_t> next_file_dialog_buffer_capacity(
    const std::size_t current_capacity,
    const std::size_t reported_required_capacity) {
  constexpr std::size_t kMaximumDialogCapacity =
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
  if (current_capacity == 0U ||
      reported_required_capacity > kMaximumDialogCapacity ||
      current_capacity >= kMaximumDialogCapacity) {
    return std::nullopt;
  }

  const std::size_t doubled =
      current_capacity > kMaximumDialogCapacity / 2U
          ? kMaximumDialogCapacity
          : current_capacity * 2U;
  const std::size_t next =
      std::max(doubled, reported_required_capacity);
  if (next <= current_capacity || next > kMaximumDialogCapacity) {
    return std::nullopt;
  }
  return next;
}

} // namespace platform_file_dialog_detail

std::optional<std::filesystem::path>
platform_open_project_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kProjectFileFilter,
      kProjectDefaultExtension);
  dialog.Flags |= OFN_FILEMUSTEXIST;
  if (GetOpenFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
    return std::filesystem::path(buffer.data());
#else
    (void)options;
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_choose_project_folder_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  return choose_folder(options);
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_save_project_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kProjectFileFilter,
      kProjectDefaultExtension);
  dialog.Flags |= OFN_OVERWRITEPROMPT;
  if (GetSaveFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
    return std::filesystem::path(buffer.data());
#else
    (void)options;
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_open_scene_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kSceneFileFilter,
      kSceneDefaultExtension);
  dialog.Flags |= OFN_FILEMUSTEXIST;
  if (GetOpenFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
  return std::filesystem::path(buffer.data());
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_save_scene_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kSceneFileFilter,
      kSceneDefaultExtension);
  dialog.Flags |= OFN_OVERWRITEPROMPT;
  if (GetSaveFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
  return std::filesystem::path(buffer.data());
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_open_material_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kMaterialFileFilter,
      kMaterialDefaultExtension);
  dialog.Flags |= OFN_FILEMUSTEXIST;
  if (GetOpenFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
  return std::filesystem::path(buffer.data());
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_save_material_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kMaterialFileFilter,
      kMaterialDefaultExtension);
  dialog.Flags |= OFN_OVERWRITEPROMPT;
  if (GetSaveFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
  return std::filesystem::path(buffer.data());
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_open_texture_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kTextureFileFilter, nullptr);
  dialog.Flags |= OFN_FILEMUSTEXIST;
  if (GetOpenFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
  return std::filesystem::path(buffer.data());
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::vector<std::filesystem::path>>
platform_open_texture_files_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::size_t capacity = kPathBufferCharacters;
  const std::wstring initial_directory = options.initial_directory.wstring();
  for (;;) {
    std::vector<wchar_t> buffer =
        path_buffer(options.initial_file_name, capacity);
    OPENFILENAMEW dialog = base_open_file_name(
        options, buffer, initial_directory, kTextureFileFilter, nullptr);
    dialog.Flags |= OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
    if (GetOpenFileNameW(&dialog) != FALSE) {
      return platform_file_dialog_detail::
          parse_explorer_file_dialog_selection(true, buffer);
    }

    if (CommDlgExtendedError() != FNERR_BUFFERTOOSMALL || buffer.empty()) {
      return std::nullopt;
    }
    static_assert(sizeof(wchar_t) >= sizeof(std::uint16_t));
    std::uint16_t reported_size = 0U;
    std::memcpy(&reported_size, buffer.data(), sizeof(reported_size));
    const std::size_t reported_required_capacity = reported_size;
    const std::optional<std::size_t> next_capacity =
        platform_file_dialog_detail::next_file_dialog_buffer_capacity(
            capacity, reported_required_capacity);
    if (!next_capacity.has_value()) {
      return std::nullopt;
    }
    capacity = *next_capacity;
  }
#else
  (void)options;
  return std::nullopt;
#endif
}

std::optional<std::filesystem::path>
platform_save_obj_file_dialog(const PlatformFileDialogOptions &options) {
#if defined(_WIN32)
  std::vector<wchar_t> buffer = path_buffer(options.initial_file_name);
  const std::wstring initial_directory = options.initial_directory.wstring();
  OPENFILENAMEW dialog = base_open_file_name(
      options, buffer, initial_directory, kObjFileFilter,
      kObjDefaultExtension);
  dialog.Flags |= OFN_OVERWRITEPROMPT;
  if (GetSaveFileNameW(&dialog) == FALSE) {
    return std::nullopt;
  }
    return std::filesystem::path(buffer.data());
#else
    (void)options;
    return std::nullopt;
#endif
}

} // namespace quader
