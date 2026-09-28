////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/project_asset_recycle.hpp"

#include <algorithm>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <propsys.h>
#include <shellapi.h>
#include <wrl/client.h>
#endif

namespace quader {
#ifdef _WIN32
namespace {

std::string shell_error(HRESULT error) {
  return "Windows recycling failed (HRESULT " +
         std::to_string(static_cast<unsigned long>(error)) + ").";
}

/// Captures the shell's actual disposition, including partial batch completion.
class RecycleProgress final : public IFileOperationProgressSink {
public:
  explicit RecycleProgress(ProjectAssetRecycleResult &result)
      : result_(result) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (out == nullptr) {
      return E_POINTER;
    }
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_IFileOperationProgressSink) {
      return E_NOINTERFACE;
    }
    *out = static_cast<IFileOperationProgressSink *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = InterlockedDecrement(&refs_);
    if (remaining == 0u) {
      delete this;
    }
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem *, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem *) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem *source,
                                           HRESULT status,
                                           IShellItem *recycled) override {
    PWSTR source_path = nullptr;
    const HRESULT named = source->GetDisplayName(SIGDN_FILESYSPATH, &source_path);
    if (SUCCEEDED(named) && source_path != nullptr) {
      for (std::size_t index = 0u; index < result_.items.size(); ++index) {
        auto &item = result_.items[index];
        if (_wcsicmp(item.original_path.lexically_normal().make_preferred().c_str(),
                     source_path) == 0) {
          item.recycled = SUCCEEDED(status) && recycled != nullptr;
          if (!item.recycled) {
            item.message = FAILED(status) ? shell_error(status)
                : "Windows did not confirm that the asset reached the Recycle Bin.";
          } else {
            PWSTR recycle_path = nullptr;
            if (SUCCEEDED(recycled->GetDisplayName(SIGDN_FILESYSPATH, &recycle_path)) &&
                recycle_path != nullptr) {
              item.recycled_path = recycle_path;
              CoTaskMemFree(recycle_path);
            }
            Microsoft::WRL::ComPtr<IShellItem2> recycled_properties;
            PROPERTYKEY key{};
            PWSTR restore_folder = nullptr;
            if (SUCCEEDED(recycled->QueryInterface(IID_PPV_ARGS(recycled_properties.GetAddressOf()))) &&
                SUCCEEDED(PSGetPropertyKeyFromName(L"System.Recycle.DeletedFrom", &key)) &&
                SUCCEEDED(recycled_properties->GetString(key, &restore_folder)) &&
                restore_folder != nullptr) {
              item.restore_directory = restore_folder;
              CoTaskMemFree(restore_folder);
            }
          }
          break;
        }
      }
      CoTaskMemFree(source_path);
    }
    // A null destination means permanent deletion. Halt subsequent items and
    // let the transaction owner restore the verified project backups.
    return FAILED(status) ? status : (recycled != nullptr ? S_OK : E_FAIL);
  }
  HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem *, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem *) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

private:
  LONG refs_ = 1;
  ProjectAssetRecycleResult &result_;
};

} // namespace
#endif

ProjectAssetRecycleResult recycle_project_asset_paths(
    std::span<const std::filesystem::path> paths) {
  ProjectAssetRecycleResult result;
  for (const auto &path : paths) {
    if (!path.is_absolute()) {
      result.message = "Recycling requires original absolute project paths.";
      return result;
    }
    result.items.push_back({.original_path = path});
  }
  if (paths.empty()) {
    result.ok = true;
    return result;
  }
#ifdef _WIN32
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized)) {
    result.message = initialized == RPC_E_CHANGED_MODE
        ? "Windows asset recycling must run on the application's STA thread."
        : shell_error(initialized);
    return result;
  }
  {
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    Microsoft::WRL::ComPtr<RecycleProgress> progress;
    progress.Attach(new RecycleProgress(result));
    HRESULT status = CoCreateInstance(CLSID_FileOperation, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(operation.GetAddressOf()));
    std::string_view phase = "creating the shell operation";
    if (SUCCEEDED(status)) {
      phase = "setting recycle-only operation flags";
      status = operation->SetOperationFlags(
          FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD | FOF_NO_CONNECTED_ELEMENTS |
          FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE);
    }
    for (const auto &path : paths) {
      if (FAILED(status)) {
        break;
      }
      Microsoft::WRL::ComPtr<IShellItem> item;
      phase = "resolving the original asset path";
      const auto shell_path = path.lexically_normal().make_preferred();
      status = SHCreateItemFromParsingName(shell_path.c_str(), nullptr,
                                           IID_PPV_ARGS(item.GetAddressOf()));
      if (SUCCEEDED(status)) {
        phase = "queueing the original asset";
        status = operation->DeleteItem(item.Get(), progress.Get());
      }
    }
    if (SUCCEEDED(status)) {
      phase = "recycling the original asset";
      status = operation->PerformOperations();
    }
    BOOL aborted = FALSE;
    const HRESULT queried = operation != nullptr
                                ? operation->GetAnyOperationsAborted(&aborted)
                                : E_FAIL;
    result.aborted = aborted != FALSE || FAILED(queried);
    result.ok = SUCCEEDED(status) && !result.aborted &&
                std::ranges::all_of(result.items, &ProjectAssetRecycleItem::recycled);
    if (!result.ok) {
      result.message = FAILED(status) ? shell_error(status) + " While " + std::string(phase) + "."
          : "Windows recycling was cancelled or did not recycle every asset.";
    }
  }
  if (SUCCEEDED(initialized)) {
    CoUninitialize();
  }
#else
  result.message = "The Windows Recycle Bin is unavailable on this platform.";
#endif
  return result;
}

} // namespace quader
