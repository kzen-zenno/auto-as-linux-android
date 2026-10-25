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

#include <csignal>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "vehicle_state_adapter.h"

#include "data_broker_feeder.h"
#include "someip_client.h"

#define SELF "[main] "

// Self pipe (for signal handling)
int pipefd[2];

void signal_handler(int signal) {
    char sig = signal;
    while (write(pipefd[1], &sig, sizeof(sig)) < 0) {
        if (errno == EINTR) {
            // If write was interupted by a signal, try again.
        } else {
            // Otherwise it doesn't make much sense to try again.
            break;
        }
    }
}

int setup_signal_handler() {
    // Setup signal handler (using a self pipe)
    if (pipe(pipefd) == -1) {
        std::cout << SELF "Failed to setup signal handler (self pipe)" << std::endl;
        std::exit(1);
    }

    auto pipe_read_fd = pipefd[0];
    auto pipe_write_fd = pipefd[1];

    // Set write end of pipe to non-blocking in order for it to work reliably in
    // the signal handler. We _do_ want the read end to remain blocking so we
    // can block while waiting for a signal.
    int flags = fcntl(pipe_write_fd, F_GETFL) | O_NONBLOCK;
    if (fcntl(pipe_write_fd, F_SETFL, flags) != 0) {
        std::cout << SELF "Failed to set self pipe to non blocking" << std::endl;
        std::exit(1);
    }
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    return pipe_read_fd;  // return read end of pipe
}

// Blocks until signal is received
void wait_for_signal(int fd) {
    char buf;
    auto res = read(fd, &buf, sizeof(buf));
    if (res < 0) {
        perror(SELF"[wait_for_signal] read() error");
    } else if (res == 1) {
        std::cerr << SELF "[wait_for_signal] Received signal: " << std::dec << (int) buf << std::endl;
    } else {
        std::cerr << SELF "[wait_for_signal] unexpected EOF" << std::endl;
    }
}

void VehicleStateAdapterRun(sdv::adapter::VehicleStateAdapter &vs_adapter) {
    auto fd = setup_signal_handler();

    vs_adapter.Start();

    std::cout << std::endl << SELF "Running VehicleState adapter... (Press Ctrl+C to stop.)" << std::endl << std::endl;
    wait_for_signal(fd);

    std::cerr << "\n" << SELF "Shutting down from signal handler..\n" << std::endl;
    vs_adapter.Shutdown();

    google::protobuf::ShutdownProtobufLibrary();
}

bool get_file_size(std::string fname, size_t& fsize) {
    struct stat statbuf;
    if (::stat(fname.c_str(), &statbuf) == -1) {
        perror("[get_file_size] Error accessing");
        fsize = 0;
        return false;
    }
    fsize = statbuf.st_size;
    return true;
}

std::string read_file(std::string fname) {
    // FIXME: Check for maximum file size before allocating memory!
    std::fstream fs(fname);
    std::stringstream ss;
    ss << fs.rdbuf();
    return ss.str();
}

void print_help(const char* application) {
    std::cout
        << "Usage: " << application << " <OPTIONS>\n"
        << "\nOPTIONS:\n"
        << "  --target=<ip>:<port>  Databroker address. [Default: localhost:55555].'\n"
        << "  --token <FILE>        Use token from specified file to authorize with Databroker.\n"
        << "  --help                This message.\n"
        << "\n\nEnvironment variables:\n"
        << "  BROKER_ADDR               Override Databroker address (host:port)\n"
        << "  BROKER_TOKEN_FILE         Use token from specified file to authorize with Databroker.\n"
        << "  BROKER_TOKEN              Use value as token to authorize with Databroker.\n"
        << "  VSOMEIP_CONFIGURATION     vsomeip json configuration file (required).\n"
        << "  VSOMEIP_APPLICATION_NAME  vsomeip application name (required).\n"
        << "  SOMEIP_CLI_DEBUG          SOME/IP Client debug level [0=OFF, 1=INFO, 2=DEBUG, 3=TRACE]\n"
        << "  DBF_DEBUG                 Databroker Feeder debug levels [0=OFF, 1=INFO, 2=DEBUG, 3=TRACE]\n"
        << "  KUKSA_DEBUG               Kuksa/GRPC debug levels [0=OFF, 1=INFO, 2=DEBUG, 3=TRACE]\n"
        << std::endl;
}

/**
 * @brief main Instantiate the VehicleState adapter (SWARCH-001). It
 * requires a channel, out of which the actual RPCs are created. This
 * channel models a connection to an endpoint specified by the argument
 * "--target=". We indicate that the channel isn't authenticated (use of
 * InsecureChannelCredentials()).
 */
int main(int argc, char** argv) {

    std::string target_str = sdv::someip::getEnvironmentStr("BROKER_ADDR", "localhost:55555");
    std::string token_file_str = sdv::someip::getEnvironmentStr("BROKER_TOKEN_FILE", {});
    std::string token_str;

    std::string arg_target("--target");
    std::string arg_token("--token");
    std::string arg_help("--help");

    for (int i = 1; i < argc; i++) {
        std::string arg_val = argv[i];
        if (arg_help == arg_val) {
            print_help(argv[0]);
            exit(0);
        } else if (arg_token == arg_val && i < argc - 1) {
            token_file_str = argv[++i];
            continue;
        } else {
            size_t start_pos = arg_val.find(arg_target);
            if (start_pos != std::string::npos) {
                start_pos += arg_target.size();
                if (arg_val[start_pos] == '=') {
                    target_str = arg_val.substr(start_pos + 1);
                    continue;
                } else {
                    std::cout << "Target argument syntax is --target=<ip>:<port>" << std::endl;
                    exit(1);
                }
            }
            // fallback for unknown arg / missing option
            std::cerr << "Invalid argument: " << arg_val << std::endl;
            print_help(argv[0]);
            exit(1);
        }
    }

    if (!token_file_str.empty()) {
        // sanity check for token filesize
        size_t token_size;
        if (!get_file_size(token_file_str, token_size)) {
            std::cerr << "Can't read token from: " << token_file_str << std::endl;
            exit(1);
        }
        std::cout << "# Reading token from " << token_file_str << ", size:" << token_size << std::endl;
        if (token_size == 0 || token_size > 16000) {
            std::cerr << "Invliad token file size!" << std::endl;
            exit(1);
        }
        token_str = read_file(token_file_str);
    }

    sdv::adapter::VehicleStateAdapter vs_adapter;
    vs_adapter.InitDataBrokerFeeder(target_str, token_str);
    if (!vs_adapter.InitSomeipClient()) {
        std::cerr << "SOME/IP not available; exiting." << std::endl;
        return 1;
    }
    VehicleStateAdapterRun(vs_adapter);
    return 0;
}
