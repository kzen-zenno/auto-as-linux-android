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

// Dev-only verification client for vehicle_state_service (SWARCH-002
// section 6 test scenarios 1-5). This is NOT the SWARCH-001 production
// provider adapter -- it reuses the existing generic
// sdv::someip::SomeIPClient (src/lib/someip_client/someip_client.h) for
// vsomeip plumbing and the new lkv_codec for decode/encode, purely to
// exercise the new server manually/scripted during this increment.

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lkv_codec.h"
#include "someip_client.h"
#include "vehicle_state_ids.h"

using namespace sdv::someip::vehicle_state;
using sdv::someip::SomeIPClient;
using sdv::someip::SomeIPConfig;

namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
int g_events_seen = 0;
bool g_response_ready = false;
std::vector<uint8_t> g_last_response;
uint64_t g_last_event_ts = 0;
bool g_ts_monotonic = true;

std::string QualityName(Quality q) {
    switch (q) {
        case Quality::kOk:      return "OK";
        case Quality::kInvalid: return "INVALID";
        case Quality::kStale:   return "STALE";
    }
    return "?";
}

void PrintEntry(const LkvEntry &e) {
    std::cout << "  key=0x" << std::hex << std::setw(4) << std::setfill('0') << e.key_id
              << std::dec << " value=" << e.value.ToString()
              << " quality=" << QualityName(e.quality)
              << " confidence=" << static_cast<int>(e.confidence)
              << " ts=" << e.timestamp_ms
              << " source=" << e.source
              << std::endl;
}

void PrintMessage(const LkvMessage &msg) {
    std::cout << "schema_version=" << msg.schema_version
              << " entries=" << msg.entries.size() << std::endl;
    for (const auto &e : msg.entries) PrintEntry(e);
}

int OnMessage(vsomeip::service_t service, vsomeip::instance_t instance,
              vsomeip::method_t method, const uint8_t *payload, size_t size) {
    (void)service;
    (void)instance;
    if (method == kEventId) {
        LkvMessage msg;
        if (!DecodeMessage(payload, size, msg)) {
            std::cerr << "[event] FAILED to decode payload (" << size << " bytes)" << std::endl;
            return -1;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_events_seen;
        std::cout << "[event #" << g_events_seen << "] ";
        PrintMessage(msg);
        const uint64_t ts = msg.entries.empty() ? 0 : msg.entries.front().timestamp_ms;
        if (g_last_event_ts != 0 && ts <= g_last_event_ts) {
            g_ts_monotonic = false;
            std::cerr << "[event] WARNING: timestamp did not increase (" << ts
                      << " <= " << g_last_event_ts << ")" << std::endl;
        }
        g_last_event_ts = ts;
        g_cv.notify_all();
    } else if (method == kMethodSnapshot || method == kMethodWrite) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last_response.assign(payload, payload + size);
        g_response_ready = true;
        g_cv.notify_all();
    }
    return 0;
}

bool WaitForResponse(std::vector<uint8_t> &out, int timeout_s = 5) {
    std::unique_lock<std::mutex> lock(g_mutex);
    if (!g_cv.wait_for(lock, std::chrono::seconds(timeout_s), [] { return g_response_ready; })) {
        return false;
    }
    out = g_last_response;
    g_response_ready = false;
    return true;
}

bool WaitForEvents(int count, int timeout_s = 30) {
    std::unique_lock<std::mutex> lock(g_mutex);
    return g_cv.wait_for(lock, std::chrono::seconds(timeout_s),
                          [count] { return g_events_seen >= count; });
}

LkvEntry MakeWriteEntry(uint16_t key_id, LkvValue value) {
    LkvEntry e;
    e.key_id = key_id;
    e.value = std::move(value);
    e.quality = Quality::kOk;
    e.confidence = Confidence::kHigh;
    e.timestamp_ms = NowMs();
    e.age_ms = 0;
    e.source = "probe";
    return e;
}

void PrintWriteResult(const std::vector<uint8_t> &bytes) {
    WriteResult result;
    LkvMessage applied;
    bool has_applied = false;
    if (!DecodeWriteResponse(bytes.data(), bytes.size(), result, applied, has_applied)) {
        std::cerr << "[write] FAILED to decode WriteResponse (" << bytes.size() << " bytes)" << std::endl;
        return;
    }
    std::cout << "[write] result_code=" << static_cast<int>(result.result_code)
              << " failed_entry_index=" << static_cast<int>(result.failed_entry_index)
              << std::endl;
    if (has_applied) PrintMessage(applied);
}

bool SendWriteAndPrint(const std::shared_ptr<SomeIPClient> &client, const LkvMessage &req) {
    std::vector<uint8_t> req_bytes;
    if (!EncodeMessage(req, req_bytes)) {
        std::cerr << "[write] failed to encode request" << std::endl;
        return false;
    }
    client->SendRequest(kServiceId, kInstanceId, kMethodWrite, req_bytes);
    std::vector<uint8_t> resp;
    if (!WaitForResponse(resp)) {
        std::cerr << "[write] no response received" << std::endl;
        return false;
    }
    PrintWriteResult(resp);
    return true;
}

void PrintUsage(const char *argv0) {
    std::cerr << "Usage: " << argv0 << " <mode> [args]\n"
              << "  --watch [N]              subscribe and print N events (default 10)\n"
              << "  --snapshot                call GetVehicleStateSnapshot\n"
              << "  --write <key>=<val> ...   call WriteVehicleState, e.g. 0x1007=22.5\n"
              << "  --malformed                send a truncated WriteVehicleState payload\n"
              << "  --unknown-key              write to a key not in the SYS-002 key table\n"
              << "  --unsupported              write to a known but non-writable key (Speed)\n"
              << "  --wrong-type               write AirconFan with a bool instead of uint8\n"
              << "  --out-of-range             write AirconFan=9 (valid range 0..7)\n";
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    const std::string mode = argv[1];

    SomeIPConfig config = SomeIPClient::createEnvConfig();
    config.use_tcp = true;
    config.service = kServiceId;
    config.instance = kInstanceId;
    config.event_group = kEventGroupId;
    config.event = kEventId;
    config.service_major = kServiceMajor;
    config.service_minor = kServiceMinor;
    config.req.use_req = true;
    config.req.service = kServiceId;
    config.req.instance = kInstanceId;
    config.req.service_major = kServiceMajor;
    config.req.service_minor = kServiceMinor;

    auto client = SomeIPClient::createInstance(config, OnMessage);
    std::thread run_thread([&client]() { client->Run(); });

    // Give registration/subscription/availability time to settle before
    // issuing requests -- this is a manual dev tool, not production code.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    int exit_code = 0;

    if (mode == "--watch") {
        const int want = (argc > 2) ? std::atoi(argv[2]) : 10;
        if (!WaitForEvents(want, 30)) {
            std::cerr << "[watch] timed out waiting for " << want << " events" << std::endl;
            exit_code = 1;
        } else {
            std::cout << "[watch] received " << g_events_seen << " events, "
                      << (g_ts_monotonic ? "timestamps strictly increasing." : "TIMESTAMP ORDER VIOLATION.")
                      << std::endl;
            if (!g_ts_monotonic) exit_code = 1;
        }
    } else if (mode == "--snapshot") {
        client->SendRequest(kServiceId, kInstanceId, kMethodSnapshot, std::vector<uint8_t>{});
        std::vector<uint8_t> resp;
        if (!WaitForResponse(resp)) {
            std::cerr << "[snapshot] no response received" << std::endl;
            exit_code = 1;
        } else {
            LkvMessage msg;
            if (!DecodeMessage(resp.data(), resp.size(), msg)) {
                std::cerr << "[snapshot] FAILED to decode response" << std::endl;
                exit_code = 1;
            } else {
                std::cout << "[snapshot] ";
                PrintMessage(msg);
            }
        }
    } else if (mode == "--write") {
        LkvMessage req;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto eq = arg.find('=');
            if (eq == std::string::npos) continue;
            const uint16_t key = static_cast<uint16_t>(std::strtoul(arg.substr(0, eq).c_str(), nullptr, 0));
            const std::string val = arg.substr(eq + 1);
            const KeyDescriptor *desc = FindKey(key);
            if (desc == nullptr) {
                std::cerr << "[write] unknown key " << arg.substr(0, eq) << ", skipping" << std::endl;
                continue;
            }
            switch (desc->type) {
                case ValueType::kBool:
                    req.entries.push_back(MakeWriteEntry(key, LkvValue::Bool(val == "1" || val == "true")));
                    break;
                case ValueType::kUint8:
                    req.entries.push_back(MakeWriteEntry(
                        key, LkvValue::Uint8(static_cast<uint8_t>(std::strtoul(val.c_str(), nullptr, 0)))));
                    break;
                case ValueType::kFloat32:
                    req.entries.push_back(MakeWriteEntry(key, LkvValue::Float32(std::strtof(val.c_str(), nullptr))));
                    break;
                default:
                    std::cerr << "[write] unsupported CLI value type for key " << arg.substr(0, eq) << std::endl;
                    break;
            }
        }
        exit_code = SendWriteAndPrint(client, req) ? 0 : 1;
    } else if (mode == "--malformed") {
        // schema_version=1, entry_count=1, but no entry bytes follow.
        const std::vector<uint8_t> bad = {0x01, 0x00, 0x01, 0x00};
        client->SendRequest(kServiceId, kInstanceId, kMethodWrite, bad);
        std::vector<uint8_t> resp;
        if (!WaitForResponse(resp)) {
            std::cerr << "[malformed] no response" << std::endl;
            exit_code = 1;
        } else {
            PrintWriteResult(resp);
        }
    } else if (mode == "--unknown-key") {
        LkvMessage req;
        req.entries.push_back(MakeWriteEntry(0x1234, LkvValue::Uint8(0)));
        exit_code = SendWriteAndPrint(client, req) ? 0 : 1;
    } else if (mode == "--unsupported") {
        LkvMessage req;
        req.entries.push_back(MakeWriteEntry(kKeySpeed, LkvValue::Float32(10.0f)));  // known, not writable
        exit_code = SendWriteAndPrint(client, req) ? 0 : 1;
    } else if (mode == "--wrong-type") {
        LkvMessage req;
        req.entries.push_back(MakeWriteEntry(kKeyAirconFan, LkvValue::Bool(true)));  // expects uint8
        exit_code = SendWriteAndPrint(client, req) ? 0 : 1;
    } else if (mode == "--out-of-range") {
        LkvMessage req;
        req.entries.push_back(MakeWriteEntry(kKeyAirconFan, LkvValue::Uint8(9)));  // valid range 0..7
        exit_code = SendWriteAndPrint(client, req) ? 0 : 1;
    } else {
        PrintUsage(argv[0]);
        exit_code = 1;
    }

    client->Shutdown();
    if (run_thread.joinable()) run_thread.join();
    return exit_code;
}
