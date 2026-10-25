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

cmake_minimum_required (VERSION 3.11)

# Project
project(VehicleStateExamples CXX)

if (NOT vsomeip3_POPULATED)
  find_package(vsomeip3)
endif()
find_package(Threads REQUIRED)

# VehicleState SOME/IP server (SWARCH-002): simulates the AUTOSAR-CP
# Gateway ECU, offering the VehicleStateService contract from SYS-002.
add_executable(vehicle_state_service
    vehicle_state_service.cc
    vehicle_state_model.cc
    write_policy.cc
)
target_link_libraries(vehicle_state_service
    vehicle_state
    vsomeip3
    pthread
)
target_include_directories(vehicle_state_service
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Dev-only verification client (not the production SWARCH-001 provider
# adapter): watches events, requests snapshots, and sends writes against
# vehicle_state_service for manual/scripted protocol testing.
add_executable(vehicle_state_probe
    vehicle_state_probe.cc
)
target_link_libraries(vehicle_state_probe
    vehicle_state
    someip_client
    vsomeip3
    pthread
)
target_include_directories(vehicle_state_probe
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS
    vehicle_state_service
    vehicle_state_probe
    RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
)

###################################################################################################
