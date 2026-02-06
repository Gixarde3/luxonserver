// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>
#include <expected>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <cstdint>
#include <csignal>

#include <luxon/enet_peer.hpp>
#include <luxon/ser_gp_binary_v18.hpp>
#include <luxon/ser_types.hpp>
#include <luxon/ser_codes.hpp>
#include <luxon/visualizer.hpp>

static bool g_running = true;

// Handle Ctrl+C
static void signal_handler(int /*signal*/) { g_running = false; }

namespace {

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void log_ser_error(const char *what, const luxon::ser::Error& err) {
    std::cerr << what << " failed: code=" << static_cast<int>(err.code) << " msg=" << err.message << std::endl;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[1]));
    std::signal(SIGINT, signal_handler);

    // Initialize Configuration
    luxon::enet::EnetPeerConfig cfg;
    cfg.time_ping_interval_ms = 1000;
    cfg.disconnect_timeout_ms = 5000;
    // cfg.crc_enabled = true;

    // Setup server
    luxon::enet::EnetServer server(cfg);

    // Setup callbacks
    server.on_peer_connected = [](std::shared_ptr<luxon::enet::EnetPeer> peer) {
        std::cout << "[Server] [" << peer->peer_id() << "] Peer connected!" << std::endl;

        // Each peer needs its own protocol instance because encryption state is per-connection.
        auto proto = std::make_shared<luxon::ser::GpBinaryV18>();

        peer->on_state_changed = [peer](luxon::enet::EnetConnectionState state) {
            std::cout << "[Server] [" << peer->peer_id() << "] State changed to: " << static_cast<int>(state) << std::endl;
            if (state == luxon::enet::EnetConnectionState::Disconnected)
                std::cout << "[Server] [" << peer->peer_id() << "] Peer disconnected!" << std::endl;
        };

        peer->on_payload_command = [peer, proto](const luxon::enet::EnetCommand& cmd) {
            // Pretty-print the incoming payload (will decrypt if the protocol has a key and the packet is encrypted).
            luxon::visualizer::print_ser_message(cmd.payload, 0, *proto);
            std::cout << std::endl;

            auto parsed = proto->Deserialize(std::span<const uint8_t>(cmd.payload.data(), cmd.payload.size()));
            if (!parsed) {
                log_ser_error("[Server] Deserialize", parsed.error());
                return;
            }

            const luxon::ser::Message& msg = *parsed;

            auto send_message = [&](const luxon::ser::Message& out_msg) {
                auto bytes = proto->Serialize(out_msg);
                if (!bytes) {
                    log_ser_error("[Server] Serialize", bytes.error());
                    return;
                }

                peer->send_payload(*bytes, luxon::enet::EnetSendOptions{
                                               .channel = 0,
                                               .mode = luxon::enet::EnetDeliveryMode::Reliable,
                                           });
            };

            std::visit(
                [&](const auto& m) {
                    using T = std::decay_t<decltype(m)>;

                    if constexpr (std::is_same_v<T, luxon::ser::InitMessage>) {
                        const bool ok = (m.protocol_major == 1 && m.protocol_minor == 8);
                        if (!ok) {
                            std::cout << "[Server] [" << peer->peer_id() << "] Client init failed (protocol " << int(m.protocol_major) << "."
                                      << int(m.protocol_minor) << ")" << std::endl;
                            peer->disconnect();
                            return;
                        }

                        luxon::ser::InitResponseMessage resp{};
                        send_message(luxon::ser::Message(resp));

                        std::cout << "[Server] [" << peer->peer_id() << "] Client init complete!" << std::endl;
                    } else if constexpr (std::is_same_v<T, luxon::ser::InternalOperationRequestMessage>) {
                        if (m.operation_code == luxon::ser::Codes::IOpInitEncryption) {
                            // Server side of encryption handshake (request/response are unencrypted).
                            auto resp = proto->HandleInitEncryptionRequest(m);
                            if (!resp) {
                                log_ser_error("[Server] HandleInitEncryptionRequest", resp.error());
                                peer->disconnect();
                                return;
                            }

                            send_message(luxon::ser::Message(*resp));

                            if (proto->has_encryption_key())
                                std::cout << "[Server] [" << peer->peer_id() << "] Established encryption!" << std::endl;
                            else
                                std::cout << "[Server] [" << peer->peer_id() << "] InitEncryption handled, but key not established." << std::endl;
                        } else if (m.operation_code == luxon::ser::Codes::IOpPing) {
                            luxon::ser::InternalOperationResponseMessage resp{};
                            resp.operation_code = luxon::ser::Codes::IOpPing;
                            resp.return_code = 0; // OK

                            // Echo client timestamp if provided.
                            try {
                                const auto& client_ts = m.parameters.at(luxon::ser::Codes::IKeyClientTimestamp);
                                resp.parameters[luxon::ser::Codes::IKeyClientTimestamp] = client_ts;
                                std::cout << "[Server] [" << peer->peer_id() << "] Got internal ping, client TS=";
                                luxon::visualizer::print_value(client_ts);
                                std::cout << std::endl;
                            } catch (const std::out_of_range&) {
                            }

                            // Fill server timestamp.
                            resp.parameters[luxon::ser::Codes::IKeyServerTimestamp] = luxon::ser::Value{static_cast<int64_t>(now_ms())};

                            send_message(luxon::ser::Message(resp));
                        } else {
                            std::cout << "[Server] [" << peer->peer_id() << "] Unhandled InternalOperationRequest opcode=" << int(m.operation_code)
                                      << std::endl;
                        }
                    } else if constexpr (std::is_same_v<T, luxon::ser::OperationRequestMessage>) {
                        std::cout << "[Server] [" << peer->peer_id() << "] OperationRequest opcode=" << int(m.operation_code) << " (no handler)" << std::endl;
                    } else if constexpr (std::is_same_v<T, luxon::ser::DisconnectMessage>) {
                        std::cout << "[Server] [" << peer->peer_id() << "] DisconnectMessage code=" << m.code << std::endl;
                    }
                },
                msg);
        };
    };

    // Initiate Connection
    std::cout << "Listening on port " << port << "..." << std::endl;
    if (!server.bind(port)) {
        std::cerr << "Failed to bind server." << std::endl;
        return 1;
    }

    // Main Service Loop
    while (g_running) {
        server.service();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "Stopping..." << std::endl;
    return 0;
}
