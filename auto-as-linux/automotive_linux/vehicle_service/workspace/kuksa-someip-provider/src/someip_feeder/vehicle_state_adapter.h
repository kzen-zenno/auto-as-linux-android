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

#ifndef VEHICLE_STATE_ADAPTER_H
#define VEHICLE_STATE_ADAPTER_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "someip_client.h"
#include "data_broker_feeder.h"
#include "actuator_subscriber.h"
#include "lkv_codec.h"

// VehicleState provider adapter (SWARCH-001). See
// specs/swarch/SWARCH-001-linux-vehicle-service.md for the quality/freshness
// policy this implements within the constraints of the vendored kuksa.val.v1.Datapoint
// proto (no confidence/source field, no STALE distinct from "no value").

namespace sdv {
namespace adapter {

class VehicleStateAdapter {
public:
    VehicleStateAdapter();
    virtual ~VehicleStateAdapter();

    bool InitDataBrokerFeeder(const std::string &databroker_addr, const std::string &auth_token);
    bool InitSomeipClient();

    void Start();
    void Shutdown();

protected:
    int on_someip_message(
                    vsomeip::service_t service,
                    vsomeip::instance_t instance,
                    vsomeip::method_t method,
                    const uint8_t *payload,
                    size_t payload_length);

    void on_actuator_change(sdv::broker_feeder::kuksa::ActuatorValues target_values);

    void quality_checker_th();

private:
    void handle_event(const sdv::someip::vehicle_state::LkvMessage &msg);
    void handle_write_response(const uint8_t *payload, size_t payload_length);

    void publish_valid(uint16_t key_id, const sdv::someip::vehicle_state::LkvValue &value, uint64_t timestamp_ms);

    struct KeyState {
        uint64_t last_lkv_timestamp_ms = 0;
        uint64_t last_receive_ms = 0;
        sdv::someip::vehicle_state::Quality published_quality = sdv::someip::vehicle_state::Quality::kInvalid;
        int consecutive_valid = 0;
    };

    std::atomic<bool> feeder_active_;

    std::string databroker_addr_;
    std::shared_ptr<sdv::broker_feeder::CollectorClient> collector_client_;

    std::shared_ptr<sdv::broker_feeder::DataBrokerFeeder> databroker_feeder_;
    std::shared_ptr<std::thread> feeder_thread_;

    std::shared_ptr<sdv::broker_feeder::kuksa::ActuatorSubscriber> actuator_subscriber_;
    std::shared_ptr<std::thread> subscriber_thread_;

    std::shared_ptr<sdv::someip::SomeIPClient> someip_client_;
    std::shared_ptr<std::thread> someip_thread_;
    std::atomic<bool> someip_active_;

    std::shared_ptr<std::thread> quality_thread_;
    std::atomic<bool> quality_thread_running_;

    std::mutex state_mutex_;
    std::map<uint16_t, KeyState> key_states_;

    std::mutex shutdown_mutex_;
    std::atomic<bool> shutdown_requested_;
    int log_level_;
};

}  // namespace adapter
}  // namespace sdv

#endif  // VEHICLE_STATE_ADAPTER_H
