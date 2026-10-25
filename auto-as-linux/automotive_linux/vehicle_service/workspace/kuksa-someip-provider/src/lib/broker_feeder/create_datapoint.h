/********************************************************************************
* Copyright (c) 2022 Contributors to the Eclipse Foundation
*
* See the NOTICE file(s) distributed with this work for additional
* information regarding copyright ownership.
*
* This program and the accompanying materials are made available under the
* terms of the Apache License 2.0 which is available at
* http://www.apache.org/licenses/LICENSE-2.0
*
* SPDX-License-Identifier: Apache-2.0
********************************************************************************/
/**
 * @file      create_datapoint.h
 * @brief     File contains helper functions to create a Datapoint structure
 *
 */
#pragma once

#include "kuksa/val/v1/types.pb.h"

namespace sdv {
namespace broker_feeder {

::kuksa::val::v1::Datapoint createDatapoint(bool value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_bool_(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(int32_t value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_int32(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(uint32_t value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_uint32(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(int64_t value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_int64(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(uint64_t value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_uint64(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(float value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_float_(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(double value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_double_(value);
    return datapoint;
}

::kuksa::val::v1::Datapoint createDatapoint(const std::string& value) {
    ::kuksa::val::v1::Datapoint datapoint;
    datapoint.set_string(value);
    return datapoint;
}

}  // namespace broker_feeder
}  // namespace sdv
