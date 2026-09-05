////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
// Standalone crash helper: never link the app, its logger, or instrumentation.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kMaximumFrames = 64;

class NativeHandle final {
public:
  explicit NativeHandle(HANDLE value = nullptr) noexcept : value_(value) {}
  ~NativeHandle() { close(); }
  NativeHandle(const NativeHandle &) = delete;
  NativeHandle &operator=(const NativeHandle &) = delete;

  [[nodiscard]] HANDLE get() const noexcept { return value_; }
  [[nodiscard]] bool valid() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  bool close() noexcept {
    if (!valid()) {
      return true;
    }
    const HANDLE value = value_;
    value_ = nullptr;
    return CloseHandle(value) == TRUE;
  }

private:
  HANDLE value_ = nullptr;
};

[[nodiscard]] DWORD last_error() noexcept {
  const DWORD error = GetLastError();
  return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

// A killed helper can leave a .part file, but never publishes an incomplete file.
class PendingArtifact final {
public:
  explicit PendingArtifact(const wchar_t *path)
      : final_path_(path), temporary_path_(final_path_ + L".part"),
        file_(CreateFileW(temporary_path_.c_str(), GENERIC_WRITE, 0, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
  ~PendingArtifact() {
    file_.close();
    if (!published_) {
      DeleteFileW(temporary_path_.c_str());
    }
  }
  PendingArtifact(const PendingArtifact &) = delete;
  PendingArtifact &operator=(const PendingArtifact &) = delete;

  [[nodiscard]] HANDLE file() const noexcept { return file_.get(); }
  [[nodiscard]] bool valid() const noexcept { return file_.valid(); }

  [[nodiscard]] DWORD publish() noexcept {
    if (FlushFileBuffers(file_.get()) != TRUE) {
      return last_error();
    }
    if (!file_.close()) {
      return last_error();
    }
    if (MoveFileExW(temporary_path_.c_str(), final_path_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != TRUE) {
      return last_error();
    }
    published_ = true;
    return ERROR_SUCCESS;
  }

private:
  std::wstring final_path_;
  std::wstring temporary_path_;
  NativeHandle file_;
  bool published_ = false;
};

struct Request final {
  DWORD process_id = 0;
  DWORD thread_id = 0;
  std::uintptr_t exception_pointer = 0;
  std::uintptr_t raw_stack_pointer = 0;
  std::size_t raw_stack_count = 0;
  const wchar_t *dump_path = nullptr;
  const wchar_t *symbols_path = nullptr;
};

[[nodiscard]] bool decimal_argument(const wchar_t *text,
                                     unsigned long long maximum,
                                     unsigned long long &value) noexcept {
  if (text == nullptr || *text == L'\0') {
    return false;
  }
  // wcstoull itself accepts whitespace and signs; the transport does not.
  for (const wchar_t *character = text; *character != L'\0'; ++character) {
    if (*character < L'0' || *character > L'9') {
      return false;
    }
  }
  errno = 0;
  wchar_t *end = nullptr;
  value = std::wcstoull(text, &end, 10);
  return errno != ERANGE && end != text && *end == L'\0' && value <= maximum;
}

[[nodiscard]] bool parse_request(int argc, wchar_t **argv,
                                 Request &request) noexcept {
  if (argc != 9 || std::wcscmp(argv[1], L"--quader-fatal-diagnostics") != 0) {
    return false;
  }
  std::array<unsigned long long, 5> values{};
  constexpr std::array<unsigned long long, 5> kMaxima{
      std::numeric_limits<DWORD>::max(), std::numeric_limits<DWORD>::max(),
      std::numeric_limits<std::uintptr_t>::max(),
      std::numeric_limits<std::uintptr_t>::max(), kMaximumFrames};
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (!decimal_argument(argv[index + 2U], kMaxima[index], values[index])) {
      return false;
    }
  }
  if (values[0] == 0 || values[1] == 0 ||
      (values[4] != 0 && values[3] == 0) || argv[7][0] == L'\0' ||
      argv[8][0] == L'\0' || std::wcscmp(argv[7], argv[8]) == 0) {
    return false;
  }
  request.process_id = static_cast<DWORD>(values[0]);
  request.thread_id = static_cast<DWORD>(values[1]);
  request.exception_pointer = static_cast<std::uintptr_t>(values[2]);
  request.raw_stack_pointer = static_cast<std::uintptr_t>(values[3]);
  request.raw_stack_count = static_cast<std::size_t>(values[4]);
  request.dump_path = argv[7];
  request.symbols_path = argv[8];
  return true;
}

[[nodiscard]] bool read_remote(HANDLE process, std::uintptr_t address,
                               void *destination, std::size_t size) noexcept {
  SIZE_T received = 0;
  return address != 0 &&
         ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address),
                            destination, size, &received) == TRUE &&
         received == size;
}

[[nodiscard]] bool read_fault_context(HANDLE process, const Request &request,
                                      CONTEXT &context) noexcept {
  EXCEPTION_POINTERS pointers{};
  return read_remote(process, request.exception_pointer, &pointers, sizeof(pointers)) &&
         read_remote(process, reinterpret_cast<std::uintptr_t>(pointers.ContextRecord),
                      &context, sizeof(context));
}

struct DumpMemoryRange final {
  std::uintptr_t base = 0;
  ULONG size = 0;
};

constexpr std::uintptr_t kMaximumStackBytes = 64U * 1024U * 1024U;

[[nodiscard]] DumpMemoryRange readable_stack_range(
    HANDLE process, std::uintptr_t stack_pointer) noexcept {
  MEMORY_BASIC_INFORMATION region{};
  if (stack_pointer == 0 ||
      VirtualQueryEx(process, reinterpret_cast<LPCVOID>(stack_pointer), &region,
                      sizeof(region)) != sizeof(region) ||
      region.State != MEM_COMMIT ||
      (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
    return {};
  }
  switch (region.Protect & 0xFFU) {
  case PAGE_READONLY:
  case PAGE_READWRITE:
  case PAGE_WRITECOPY:
  case PAGE_EXECUTE_READ:
  case PAGE_EXECUTE_READWRITE:
  case PAGE_EXECUTE_WRITECOPY:
    break;
  default:
    return {};
  }
  const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
  if (region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - base) {
    return {};
  }
  const auto end = base + region.RegionSize;
  if (stack_pointer < base || stack_pointer >= end) {
    return {};
  }
  const auto available = end - stack_pointer;
  return {.base = stack_pointer,
          .size = static_cast<ULONG>(available < kMaximumStackBytes
                                         ? available : kMaximumStackBytes)};
}

struct DumpCallbackContext final {
  const Request *request = nullptr;
  std::array<DumpMemoryRange, 2> stack_ranges{};
  std::size_t range_count = 0;
  std::size_t next_range = 0;
};

[[nodiscard]] DumpCallbackContext make_dump_context(
    HANDLE process, const Request &request) noexcept {
  DumpCallbackContext result{.request = &request};
  CONTEXT fault{};
  std::uintptr_t fault_stack_pointer = 0;
  if (read_fault_context(process, request, fault)) {
#if defined(_M_X64)
    fault_stack_pointer = static_cast<std::uintptr_t>(fault.Rsp);
#elif defined(_M_IX86)
    fault_stack_pointer = static_cast<std::uintptr_t>(fault.Esp);
#elif defined(_M_ARM64)
    fault_stack_pointer = static_cast<std::uintptr_t>(fault.Sp);
#endif
  }
  const std::array stack_pointers{fault_stack_pointer, request.raw_stack_pointer};
  for (const auto pointer : stack_pointers) {
    const auto range = readable_stack_range(process, pointer);
    if (range.size == 0) {
      continue;
    }
    if (result.range_count != 0) {
      auto &previous = result.stack_ranges[0];
      const auto previous_end = previous.base + previous.size;
      const auto end = range.base + range.size;
      const auto combined_base = previous.base < range.base ? previous.base : range.base;
      const auto combined_end = previous_end > end ? previous_end : end;
      if (range.base <= previous_end && previous.base <= end &&
          combined_end - combined_base <= kMaximumStackBytes) {
        previous = {.base = combined_base,
                    .size = static_cast<ULONG>(combined_end - combined_base)};
        continue;
      }
    }
    result.stack_ranges[result.range_count++] = range;
  }
  return result;
}

BOOL CALLBACK include_dump_thread(PVOID parameter,
                                  const PMINIDUMP_CALLBACK_INPUT input,
                                  PMINIDUMP_CALLBACK_OUTPUT output) {
  if (parameter == nullptr || input == nullptr) {
    return FALSE;
  }
  auto &context = *static_cast<DumpCallbackContext *>(parameter);
  if (input->CallbackType == IncludeThreadCallback) {
    // IncludeThreadCallback excludes before suspension. The fatal coordinator
    // must stay runnable to enforce its deadline even if DbgHelp stalls.
    // Keep its exception context plus readable stack through separate streams.
    return input->IncludeThread.ThreadId != context.request->thread_id;
  }
  if (input->CallbackType == MemoryCallback) {
    if (output == nullptr || context.next_range >= context.range_count) {
      return FALSE;
    }
    const auto &range = context.stack_ranges[context.next_range++];
    output->MemoryBase = static_cast<ULONG64>(range.base);
    output->MemorySize = range.size;
    return TRUE;
  }
  return TRUE;
}

[[nodiscard]] DWORD write_dump(HANDLE process, const Request &request) {
  PendingArtifact dump(request.dump_path);
  if (!dump.valid()) {
    return last_error();
  }
  MINIDUMP_EXCEPTION_INFORMATION exception{};
  MINIDUMP_EXCEPTION_INFORMATION *exception_argument = nullptr;
  if (request.exception_pointer != 0) {
    exception.ThreadId = request.thread_id;
    exception.ExceptionPointers =
        reinterpret_cast<EXCEPTION_POINTERS *>(request.exception_pointer);
    exception.ClientPointers = TRUE;
    exception_argument = &exception;
  }
  constexpr auto kDumpFlags = static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal | MiniDumpWithHandleData | MiniDumpWithUnloadedModules |
      MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo |
      MiniDumpWithFullMemoryInfo | MiniDumpWithIndirectlyReferencedMemory);
  // Query only readable ranges; never suspend the parent to gather its stack.
  auto callback_context = make_dump_context(process, request);
  MINIDUMP_CALLBACK_INFORMATION callbacks{};
  callbacks.CallbackRoutine = include_dump_thread;
  callbacks.CallbackParam = &callback_context;
  if (MiniDumpWriteDump(process, request.process_id, dump.file(), kDumpFlags,
                        exception_argument, nullptr, &callbacks) != TRUE) {
    return last_error();
  }
  return dump.publish();
}

struct StackFrames final {
  std::array<std::uintptr_t, kMaximumFrames> addresses{};
  std::size_t count = 0;
  bool fault_context = false;
};

[[nodiscard]] StackFrames read_raw_stack(HANDLE process,
                                         const Request &request) noexcept {
  StackFrames frames;
  if (request.raw_stack_count != 0 &&
      read_remote(process, request.raw_stack_pointer, frames.addresses.data(),
                   request.raw_stack_count * sizeof(std::uintptr_t))) {
    frames.count = request.raw_stack_count;
  }
  return frames;
}

[[nodiscard]] StackFrames walk_fault_stack(HANDLE process,
                                           const Request &request,
                                           bool symbols_initialized) noexcept {
  StackFrames frames;
  CONTEXT context{};
  if (!read_fault_context(process, request, context)) {
    return frames;
  }
  STACKFRAME64 frame{};
  DWORD machine_type = 0;
#if defined(_M_X64)
  machine_type = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = context.Rip;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
  machine_type = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset = context.Eip;
  frame.AddrFrame.Offset = context.Ebp;
  frame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64)
  machine_type = IMAGE_FILE_MACHINE_ARM64;
  frame.AddrPC.Offset = context.Pc;
  frame.AddrFrame.Offset = context.Fp;
  frame.AddrStack.Offset = context.Sp;
#else
  return frames;
#endif
  if (frame.AddrPC.Offset == 0) {
    return frames;
  }
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  frames.fault_context = true;
  frames.addresses[frames.count++] = static_cast<std::uintptr_t>(frame.AddrPC.Offset);

  NativeHandle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                 FALSE, request.thread_id));
  if (!symbols_initialized || !thread.valid() ||
      GetProcessIdOfThread(thread.get()) != request.process_id) {
    return frames;
  }
  DWORD64 previous_pc = frame.AddrPC.Offset;
  DWORD64 previous_sp = frame.AddrStack.Offset;
  for (std::size_t attempt = 0; attempt < kMaximumFrames &&
                                frames.count < kMaximumFrames; ++attempt) {
    if (StackWalk64(machine_type, process, thread.get(), &frame, &context, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) != TRUE ||
        frame.AddrPC.Offset == 0) {
      break;
    }
    if (frame.AddrPC.Offset == previous_pc && frame.AddrStack.Offset == previous_sp) {
      // Some architectures first return the seed frame. Permit that once.
      if (attempt == 0) {
        continue;
      }
      break;
    }
    frames.addresses[frames.count++] = static_cast<std::uintptr_t>(frame.AddrPC.Offset);
    previous_pc = frame.AddrPC.Offset;
    previous_sp = frame.AddrStack.Offset;
  }
  return frames;
}

void append_address(std::ostringstream &text, HANDLE process,
                     std::uintptr_t address, bool symbols_initialized) {
  text << "0x" << std::uppercase << std::hex << address << std::dec;
  if (!symbols_initialized) {
    return;
  }
  IMAGEHLP_MODULE64 module{};
  module.SizeOfStruct = sizeof(module);
  if (SymGetModuleInfo64(process, static_cast<DWORD64>(address), &module) == TRUE) {
    text << " [module=" << module.ModuleName << " rva=0x" << std::hex
         << (static_cast<DWORD64>(address) - module.BaseOfImage) << std::dec << "]";
  }
  alignas(SYMBOL_INFO) std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};
  auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage.data());
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  if (SymFromAddr(process, static_cast<DWORD64>(address), &displacement, symbol) == TRUE) {
    text << " " << std::string_view(symbol->Name, symbol->NameLen);
    if (displacement != 0) {
      text << "+0x" << std::hex << displacement << std::dec;
    }
  }
  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(line);
  DWORD line_displacement = 0;
  if (SymGetLineFromAddr64(process, static_cast<DWORD64>(address),
                           &line_displacement, &line) == TRUE && line.FileName != nullptr) {
    text << " (" << line.FileName << ":" << line.LineNumber << ")";
  }
}

[[nodiscard]] DWORD publish_text(const wchar_t *path, std::string_view text) {
  PendingArtifact artifact(path);
  if (!artifact.valid()) {
    return last_error();
  }
  if (text.size() > std::numeric_limits<DWORD>::max()) {
    return ERROR_FILE_TOO_LARGE;
  }
  DWORD written = 0;
  if (WriteFile(artifact.file(), text.data(), static_cast<DWORD>(text.size()),
                 &written, nullptr) != TRUE) {
    return last_error();
  }
  if (written != text.size()) {
    return ERROR_WRITE_FAULT;
  }
  return artifact.publish();
}

void write_symbols(HANDLE process, const Request &request) {
  const StackFrames raw_frames = read_raw_stack(process, request);
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES |
                SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
  const bool initialized = SymInitialize(process, nullptr, TRUE) == TRUE;
  StackFrames frames = walk_fault_stack(process, request, initialized);
  if (frames.count == 0) {
    frames = raw_frames;
  }
  std::ostringstream text;
  text << "Stack trace (symbolized "
       << (frames.fault_context ? "fault context" : "captured parent frames")
       << ", most recent call first):";
  if (!initialized) {
    text << "\n  Symbols unavailable; retaining target addresses.";
  }
  if (frames.count == 0) {
    text << "\n  No target stack frames could be read.";
  }
  for (std::size_t index = 0; index < frames.count; ++index) {
    text << "\n  #" << std::setw(2) << std::setfill('0') << index << " ";
    append_address(text, process, frames.addresses[index], initialized);
  }
  text << '\n';
  static_cast<void>(publish_text(request.symbols_path, text.str()));
  // Publish before cleanup as DbgHelp can itself block; the parent owns its deadline.
  if (initialized) {
    SymCleanup(process);
  }
}

[[nodiscard]] DWORD run(const Request &request) {
  NativeHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                    PROCESS_DUP_HANDLE, FALSE, request.process_id));
  if (!process.valid()) {
    return last_error();
  }
  // Do not initialize symbols before producing the recoverable dump artifact.
  const DWORD dump_result = write_dump(process.get(), request);
  try {
    write_symbols(process.get(), request);
  } catch (...) {
    // Symbol or text failures never override a successfully published minidump.
  }
  return dump_result;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    Request request;
    if (!parse_request(argc, argv, request)) {
      return static_cast<int>(ERROR_INVALID_PARAMETER);
    }
    return static_cast<int>(run(request));
  } catch (...) {
    return static_cast<int>(ERROR_GEN_FAILURE);
  }
}
