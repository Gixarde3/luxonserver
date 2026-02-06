// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>

#include <luxon/enet_peer.hpp>
#include <luxon/ser_gp_binary_v18.hpp>
#include <luxon/ser_types.hpp>

static bool g_running = true;

// Handle Ctrl+C
static void signal_handler(int /*signal*/) { g_running = false; }

static void send_bytes(luxon::enet::EnetPeer& peer, const luxon::ser::ByteArray& bytes) {
    peer.send_payload(bytes, luxon::enet::EnetSendOptions{
                                 .channel = 0,
                                 .mode = luxon::enet::EnetDeliveryMode::Reliable,
                             });
}

static void send_message(luxon::enet::EnetPeer& peer, luxon::ser::IProtocol& protocol, const luxon::ser::Message& msg) {
    auto pkt = protocol.Serialize(msg);
    if (!pkt) {
        std::cerr << "Serialize failed: " << pkt.error().message << "\n";
        return;
    }
    send_bytes(peer, *pkt);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <host> <port>\n";
        return 1;
    }

    const std::string host = argv[1];
    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));

    std::signal(SIGINT, signal_handler);

    // Initialize ENet-style configuration
    luxon::enet::EnetPeerConfig cfg;
    cfg.time_ping_interval_ms = 1000;
    cfg.disconnect_timeout_ms = 5000;

    // Setup Socket and Peer
    luxon::enet::UdpSocket sock;
    luxon::enet::EnetPeer peer(cfg);
    luxon::ser::GpBinaryV18 protocol;

    peer.on_state_changed = [](luxon::enet::EnetConnectionState state) {
        std::cout << "[Client] State changed to: " << static_cast<int>(state) << "\n";
        if (state == luxon::enet::EnetConnectionState::Disconnected)
            g_running = false;
    };

    peer.on_payload_command = [&protocol](const luxon::enet::EnetCommand& cmd) {
        const auto& payload = cmd.payload;
        std::span<const uint8_t> packet_bytes(payload.data(), payload.size());

        auto msg = protocol.Deserialize(packet_bytes);
        if (!msg) {
            std::cerr << "Deserialize failed: " << msg.error().message << "\n";
            return;
        }

        // We only care about the encryption handshake response here
        if (auto *resp = std::get_if<luxon::ser::InternalOperationResponseMessage>(&(*msg))) {
            auto ok = protocol.HandleInitEncryptionResponse(*resp);
            if (!ok) {
                std::cerr << "HandleInitEncryptionResponse failed: " << ok.error().message << std::endl;
                return;
            }

            std::cout << "Established encryption!" << std::endl;
        }
    };

    // Initiate Connection
    std::cout << "Connecting to " << host << ":" << port << "...\n";
    if (!peer.connect(sock, host, port)) {
        std::cerr << "Failed to initialize connection request.\n";
        return 1;
    }

    // 1) Send Init message (unencrypted)
    {
        luxon::ser::InitMessage init{.app_id = "NameServer"};

        send_message(peer, protocol, luxon::ser::Message(init, false));
    }

    // 2) Begin crypto handshake (unencrypted InternalOperationRequest)
    {
        auto req_bytes = protocol.CreateInitEncryptionRequest();
        if (!req_bytes) {
            std::cerr << "CreateInitEncryptionRequest failed: " << req_bytes.error().message << "\n";
            return 1;
        }

        std::cout << "Trying to establish encryption..." << std::endl;
        send_bytes(peer, *req_bytes);
    }

    // Main Service Loop
    uint8_t buffer[2048];
    while (g_running) {
        luxon::enet::EnetEndpoint from;
        const size_t received = sock.recv_from(buffer, sizeof(buffer), from);
        if (received > 0) {
            luxon::ser::ByteArray datagram(buffer, buffer + received);
            peer.handle_incoming_datagram(datagram);
        }

        peer.service();

        while (peer.dispatch_one()) {
            // callbacks handle the messages
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Disconnecting...\n";
    peer.disconnect();
    return 0;
}
