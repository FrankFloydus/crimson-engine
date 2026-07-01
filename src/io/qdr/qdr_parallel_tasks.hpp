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
void runParallelIndexTasks(std::size_t count, std::string_view label, Work&& work) {
    if (count < kQdrParallelTaskThreshold) {
        for (std::size_t index = 0; index < count; ++index) {
            work(index);
        }
        return;
    }

    const std::size_t hardwareThreads = std::max<std::size_t>(1u, std::thread::hardware_concurrency());
    const std::size_t taskCount = std::min<std::size_t>(count, std::min<std::size_t>(hardwareThreads, 8u));
    const std::size_t chunkSize = (count + taskCount - 1u) / taskCount;

    std::mutex errorMutex;
    std::string firstError;

    tf::Executor executor(taskCount);
    tf::Taskflow taskflow;
    for (std::size_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
        const std::size_t begin = taskIndex * chunkSize;
        const std::size_t end = std::min(count, begin + chunkSize);
        if (begin >= end) {
            continue;
        }
        taskflow.emplace([&, begin, end] {
            for (std::size_t index = begin; index < end; ++index) {
                try {
                    work(index);
                } catch (const std::exception& error) {
                    std::lock_guard lock(errorMutex);
                    if (firstError.empty()) {
                        firstError = std::string(label) + " task " + std::to_string(index) + " failed: " + error.what();
                    }
                    return;
                } catch (...) {
                    std::lock_guard lock(errorMutex);
                    if (firstError.empty()) {
                        firstError = std::string(label) + " task " + std::to_string(index) + " failed.";
                    }
                    return;
                }
            }
        });
    }

    executor.run(taskflow).wait();
    if (!firstError.empty()) {
        throw std::runtime_error(firstError);
    }
}

} // namespace quader::qdr_detail
