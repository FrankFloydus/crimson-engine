################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
cmake_minimum_required(VERSION 3.25)

project(QuaderModelingSdkExternalConsumer LANGUAGES CXX)

find_package(QuaderModelingSdk CONFIG REQUIRED)

add_executable(quader_modeling_sdk_external_consumer main.cpp)
target_compile_features(quader_modeling_sdk_external_consumer PRIVATE cxx_std_20)
target_link_libraries(quader_modeling_sdk_external_consumer
    PRIVATE
        quader::modeling_sdk)
