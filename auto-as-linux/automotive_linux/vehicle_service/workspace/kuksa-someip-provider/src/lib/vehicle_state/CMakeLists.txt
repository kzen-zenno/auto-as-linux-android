#********************************************************************************
# Copyright (c) 2022 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License 2.0 which is available at
# http://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
#*******************************************************************************/

project(vehicle_state)

cmake_minimum_required(VERSION 3.11)

# VehicleState SOME/IP contract: ids/constants + LKV codec (SYS-002 / SWARCH-001 / SWARCH-002)
add_library(vehicle_state
  EXCLUDE_FROM_ALL
  STATIC
    lkv_codec.cc
    fixed_lkv_codec.cc
)

target_include_directories(vehicle_state
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# std::variant (LkvValue) requires C++17; PUBLIC so it propagates to
# executables linking this library (this project sets no project-wide
# CXX standard, and Ubuntu 20.04's default GCC defaults to gnu++14).
target_compile_features(vehicle_state PUBLIC cxx_std_17)

install(TARGETS vehicle_state
  PRIVATE_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
