////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace quader::qdr_detail {

inline constexpr std::size_t kQdrParallelTaskThreshold = 4096u;

template <typename Work>
void run_parallel_index_tasks(std::size_t count, std::string_view label,
                              Work &&work) {
  if (count < kQdrParallelTaskThreshold) {
    for (std::size_t index = 0; index < count; ++index) {
      work(index);
    }
    return;
  }

  const std::size_t hardware_threads =
      std::max<std::size_t>(1u, std::thread::hardware_concurrency());
  const std::size_t task_count =
      std::min<std::size_t>(count, std::min<std::size_t>(hardware_threads, 8u));
  const std::size_t chunk_size = (count + task_count - 1u) / task_count;

  std::mutex error_mutex;
  std::string first_error;

  tf::Executor executor(task_count);
  tf::Taskflow taskflow;
  for (std::size_t task_index = 0; task_index < task_count; ++task_index) {
    const std::size_t begin = task_index * chunk_size;
    const std::size_t end = std::min(count, begin + chunk_size);
    if (begin >= end) {
      continue;
    }
    taskflow.emplace([&, begin, end] {
      for (std::size_t index = begin; index < end; ++index) {
        try {
          work(index);
        } catch (const std::exception &error) {
          std::lock_guard lock(error_mutex);
          if (first_error.empty()) {
            first_error = std::string(label) + " task " +
                          std::to_string(index) + " failed: " + error.what();
          }
          return;
        } catch (...) {
          std::lock_guard lock(error_mutex);
          if (first_error.empty()) {
            first_error = std::string(label) + " task " +
                          std::to_string(index) + " failed.";
          }
          return;
        }
      }
    });
  }

  executor.run(taskflow).wait();
  if (!first_error.empty()) {
    throw std::runtime_error(first_error);
  }
}

} // namespace quader::qdr_detail
