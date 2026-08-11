////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/platform_shell.hpp"

#include <cstdint>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace quader {
namespace {

#if defined(_WIN32)
std::filesystem::path nearest_existing_parent(
    const std::filesystem::path &path) {
  std::filesystem::path candidate = path.parent_path();
  while (!candidate.empty()) {
    std::error_code error;
    if (std::filesystem::is_directory(candidate, error) && !error) {
      return candidate;
    }
    const std::filesystem::path next = candidate.parent_path();
    if (next == candidate) {
      break;
    }
    candidate = next;
  }
  return {};
}
#endif

} // namespace

bool platform_reveal_file(const std::filesystem::path &path) {
#if defined(_WIN32)
  if (path.empty()) {
    return false;
  }

  const HRESULT initialized =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool uninitialize = SUCCEEDED(initialized);
  const bool com_available =
      SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE;
  const auto finish = [uninitialize](bool result) {
    if (uninitialize) {
      CoUninitialize();
    }
    return result;
  };

  std::error_code error;
  const bool target_exists = std::filesystem::exists(path, error);
  if (com_available && target_exists && !error) {
    PIDLIST_ABSOLUTE item = nullptr;
    const std::wstring path_text = path.wstring();
    if (SUCCEEDED(SHParseDisplayName(path_text.c_str(), nullptr, &item, 0,
                                     nullptr)) &&
        item != nullptr) {
      const HRESULT revealed = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
      CoTaskMemFree(item);
      if (SUCCEEDED(revealed)) {
        return finish(true);
      }
    }
  }

  const std::filesystem::path parent = nearest_existing_parent(path);
  if (parent.empty()) {
    return finish(false);
  }
  const std::wstring parent_text = parent.wstring();
  const HINSTANCE opened = ShellExecuteW(nullptr, L"open", parent_text.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
  return finish(reinterpret_cast<std::intptr_t>(opened) > 32);
#else
  (void)path;
  return false;
#endif
}

} // namespace quader
