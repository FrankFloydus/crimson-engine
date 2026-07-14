////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/platform_file_dialog.hpp"

#include <algorithm>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
constexpr wchar_t kObjFileFilter[] =
    L"Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
constexpr wchar_t kObjDefaultExtension[] = L"obj";
constexpr std::size_t kPathBufferCharacters = 32768U;

std::vector<wchar_t> path_buffer(const std::filesystem::path &initial_path) {
  std::vector<wchar_t> buffer(kPathBufferCharacters, L'\0');
  const std::wstring text = initial_path.wstring();
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
