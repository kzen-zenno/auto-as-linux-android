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

// VehicleState SOME/IP server (SWARCH-002 section 5 "Event loop and
// shutdown" / section 4 "Method behavior"): offer thread + notify thread +
// signal handling, TCP-only reliable transport (no UDP/multicast event
// path), the LKV codec, and the snapshot/write methods.

#include <csignal>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <thread>

#include <vsomeip/vsomeip.hpp>

#include "lkv_codec.h"
#include "vehicle_state_ids.h"
#include "vehicle_state_model.h"
#include "write_policy.h"

using namespace sdv::someip::vehicle_state;

namespace {
int debug = ::getenv("DEBUG") ? ::atoi(::getenv("DEBUG")) : 0;
}

class vehicle_state_service {
public:
    vehicle_state_service()
        // No explicit name: vsomeip reads VSOMEIP_APPLICATION_NAME, which
        // must match the "applications" entry in VSOMEIP_CONFIGURATION.
        : app_(vsomeip::runtime::get()->create_application()),
          running_(true),
          is_offered_(false),
          offer_thread_(std::bind(&vehicle_state_service::run, this)),
          notify_thread_(std::bind(&vehicle_state_service::notify_th, this)) {
        pthread_setname_np(offer_thread_.native_handle(), "vs_offer");
        pthread_setname_np(notify_thread_.native_handle(), "vs_notify");
    }

    bool init() {
        std::lock_guard<std::mutex> its_lock(mutex_);

        if (!app_->init()) {
            std::cerr << "Couldn't initialize application" << std::endl;
            return false;
        }
        app_->register_state_handler(
            std::bind(&vehicle_state_service::on_state, this, std::placeholders::_1));

        std::set<vsomeip::eventgroup_t> its_groups;
        its_groups.insert(kEventGroupId);
        app_->offer_event(
            kServiceId, kInstanceId, kEventId, its_groups,
            // ET_FIELD, matching src/lib/someip_client/someip_client.cc's
            // init_event_service(), which hardcodes ET_FIELD on the
            // request_event() side; the reused generic client (see
            // vehicle_state_probe.cc) assumes this.
            vsomeip::event_type_e::ET_FIELD, std::chrono::milliseconds::zero(),
            false, true, nullptr,
            // TCP-only per SYS-002 5.1: no RT_UNRELIABLE fallback.
            vsomeip::reliability_type_e::RT_RELIABLE);

        {
            std::lock_guard<std::mutex> its_payload_lock(payload_mutex_);
            payload_ = vsomeip::runtime::get()->create_payload();
        }

        app_->register_message_handler(
            kServiceId, kInstanceId, kMethodSnapshot,
            std::bind(&vehicle_state_service::on_snapshot_request, this, std::placeholders::_1));
        app_->register_message_handler(
            kServiceId, kInstanceId, kMethodWrite,
            std::bind(&vehicle_state_service::on_write_request, this, std::placeholders::_1));

        blocked_ = true;
        condition_.notify_one();
        return true;
    }

    void start() { app_->start(); }

    void stop() {
        running_ = false;
        blocked_ = true;
        condition_.notify_one();
        notify_condition_.notify_one();
        app_->clear_all_handler();
        stop_offer();
        if (offer_thread_.joinable()) offer_thread_.join();
        if (notify_thread_.joinable()) notify_thread_.join();
        app_->stop();
    }

    void on_state(vsomeip::state_type_e _state) {
        const bool registered = (_state == vsomeip::state_type_e::ST_REGISTERED);
        std::cout << "Application " << app_->get_name() << " is "
                  << (registered ? "registered." : "deregistered.") << std::endl;
    }

    void on_snapshot_request(const std::shared_ptr<vsomeip::message> &_request) {
        // GetVehicleStateSnapshot (SYS-002 5.2 / SWARCH-002 4): request is
        // empty or a bare uint16 schema_version; response is always the
        // current snapshot -- there is no defined error response format
        // for this method.
        auto its_response = vsomeip::runtime::get()->create_response(_request);
        LkvMessage msg;
        msg.entries = model_.Snapshot();
        std::vector<uint8_t> bytes;
        if (!EncodeMessage(msg, bytes)) {
            std::cerr << "### [snapshot] failed to encode response!" << std::endl;
            return;
        }
        auto resp_payload = vsomeip::runtime::get()->create_payload();
        resp_payload->set_data(bytes);
        its_response->set_payload(resp_payload);
        app_->send(its_response);
        if (debug > 1) {
            std::cout << "### [snapshot] served " << msg.entries.size() << " entries" << std::endl;
        }
    }

    void on_write_request(const std::shared_ptr<vsomeip::message> &_request) {
        auto its_payload = _request->get_payload();

        LkvMessage decoded;
        WriteResult result;
        const LkvMessage *applied_state = nullptr;
        LkvMessage applied_msg;

        if (!DecodeMessage(its_payload->get_data(), its_payload->get_length(), decoded)) {
            // Malformed payload: no specific entry can be identified.
            result.result_code = WriteResultCode::kInvalid;
            result.failed_entry_index = 0x00;
        } else {
            ValidatedWrite validated = ValidateWrite(decoded);
            result = validated.result;
            if (result.result_code == WriteResultCode::kOk) {
                model_.ApplyWrite(validated.applied_entries);
                applied_msg.entries = validated.applied_entries;
                applied_state = &applied_msg;
            }
        }

        std::vector<uint8_t> bytes;
        if (!EncodeWriteResponse(result, applied_state, bytes)) {
            std::cerr << "### [write] failed to encode response!" << std::endl;
            return;
        }

        auto its_response = vsomeip::runtime::get()->create_response(_request);
        auto resp_payload = vsomeip::runtime::get()->create_payload();
        resp_payload->set_data(bytes);
        its_response->set_payload(resp_payload);
        app_->send(its_response);

        if (debug > 0) {
            std::cout << "### [write] result_code=" << static_cast<int>(result.result_code)
                      << " failed_entry_index=" << static_cast<int>(result.failed_entry_index)
                      << std::endl;
        }
    }

    void run() {
        std::unique_lock<std::mutex> its_lock(mutex_);
        while (!blocked_) condition_.wait(its_lock);
        its_lock.unlock();

        offer();
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    void notify_th() {
        while (running_) {
            std::unique_lock<std::mutex> its_lock(notify_mutex_);
            while (!is_offered_ && running_) notify_condition_.wait(its_lock);
            its_lock.unlock();

            while (is_offered_ && running_) {
                model_.Tick();
                LkvMessage msg;
                msg.entries = model_.Snapshot();

                std::vector<uint8_t> bytes;
                if (EncodeMessage(msg, bytes)) {
                    {
                        std::lock_guard<std::mutex> its_payload_lock(payload_mutex_);
                        payload_->set_data(bytes);
                    }
                    // Model lock is not held during the vsomeip call
                    // (SWARCH-002 5): notify() only touches payload_.
                    app_->notify(kServiceId, kInstanceId, kEventId, payload_);
                    if (debug > 2) {
                        std::cout << "### notify " << msg.entries.size() << " entries, "
                                  << bytes.size() << " bytes" << std::endl;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kEventCycleMs));
            }
        }
    }

private:
    void offer() {
        std::lock_guard<std::mutex> its_lock(notify_mutex_);
        std::printf("Application %s offering VehicleStateService [%04x.%04x] v%u.%u\n",
                    app_->get_name().c_str(), kServiceId, kInstanceId, kServiceMajor, kServiceMinor);
        app_->offer_service(kServiceId, kInstanceId, kServiceMajor, kServiceMinor);
        is_offered_ = true;
        notify_condition_.notify_one();
    }

    void stop_offer() {
        std::printf("Application %s stop offering [%04x.%04x] v%u.%u\n",
                    app_->get_name().c_str(), kServiceId, kInstanceId, kServiceMajor, kServiceMinor);
        app_->stop_offer_service(kServiceId, kInstanceId, kServiceMajor, kServiceMinor);
        is_offered_ = false;
    }

    std::shared_ptr<vsomeip::application> app_;
    bool running_;

    std::mutex mutex_;
    std::condition_variable condition_;
    bool blocked_ = false;

    std::mutex notify_mutex_;
    std::condition_variable notify_condition_;
    bool is_offered_;

    std::mutex payload_mutex_;
    std::shared_ptr<vsomeip::payload> payload_;

    // blocked_ / is_offered_ must be initialized before starting the threads!
    std::thread offer_thread_;
    std::thread notify_thread_;

    VehicleStateModel model_;
};

namespace {
vehicle_state_service *its_service_ptr(nullptr);

void handle_signal(int _signal) {
    if (its_service_ptr != nullptr && (_signal == SIGINT || _signal == SIGTERM)) {
        its_service_ptr->stop();
    }
}
}  // namespace

int main(int, char **) {
    const char *app_name = ::getenv("VSOMEIP_APPLICATION_NAME");
    if (!app_name) {
        std::cerr << "Environment variable VSOMEIP_APPLICATION_NAME not set!" << std::endl;
        return 1;
    }
    const char *app_config = ::getenv("VSOMEIP_CONFIGURATION");
    if (!app_config) {
        std::cerr << "Environment variable VSOMEIP_CONFIGURATION not set!" << std::endl;
        return 1;
    }

    vehicle_state_service its_service;
    its_service_ptr = &its_service;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (its_service.init()) {
        its_service.start();
        return 0;
    }
    its_service.stop();
    return 1;
}
