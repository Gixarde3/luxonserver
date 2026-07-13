// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "luxon_enet_c.h"

#include "luxon/enet_peer.hpp"
#include "luxon/enet_protocol.hpp"

#ifdef LUXON_ENET_ENABLE_METRICS
#include "luxon/enet_metrics.hpp"
#endif

#include <string_view>
#include <span>
#include <memory>
#include <cstring>

// C++ internal wrappers

struct luxon_enet_peer {
    std::shared_ptr<luxon::enet::EnetPeer> peer;
    std::unique_ptr<luxon::enet::UdpSocket> client_sock; // Only set for standalone clients

    void *user_data = nullptr;
    luxon_enet_peer_on_state_changed_cb on_state = nullptr;
    luxon_enet_peer_on_payload_cb on_payload = nullptr;
    luxon_enet_peer_on_log_cb on_log = nullptr;

    bool is_server_managed = false;

    void hook_callbacks() {
        if (!peer)
            return;

        peer->on_state_changed = [this](luxon::enet::EnetConnectionState st) {
            if (this->on_state) {
                this->on_state(this, static_cast<luxon_enet_connection_state_t>(st), this->user_data);
            }
        };

        peer->on_payload_command = [this](luxon::enet::EnetCommand&& cmd) {
            if (this->on_payload) {
                auto payload = cmd.get_payload();
                this->on_payload(this, payload.data(), payload.size(), this->user_data);
            }
        };

        peer->on_log_message = [this](luxon::enet::LogLevel lvl, std::string_view msg) {
            if (this->on_log) {
                std::string null_terminated(msg);
                this->on_log(this, static_cast<luxon_enet_log_level_t>(lvl), null_terminated.c_str(), this->user_data);
            }
        };
    }
};

struct luxon_enet_server {
    std::unique_ptr<luxon::enet::EnetServer> server;

#ifdef LUXON_ENET_ENABLE_METRICS
    luxon::enet::Metrics metrics;
#endif

    void *user_data = nullptr;
    luxon_enet_server_on_peer_connected_cb on_connect = nullptr;
};

// Internal Helper
static void cpp_config_from_c(luxon::enet::EnetPeerConfig& cpp_cfg, const luxon_enet_peer_config_t *c_cfg) {
    if (!c_cfg)
        return;
    cpp_cfg.mtu = c_cfg->mtu;
    cpp_cfg.channel_count = c_cfg->channel_count;
    cpp_cfg.crc_enabled = c_cfg->crc_enabled;
    cpp_cfg.time_base = c_cfg->time_base;
    cpp_cfg.time_ping_interval_ms = c_cfg->time_ping_interval_ms;
    cpp_cfg.disconnect_timeout_ms = c_cfg->disconnect_timeout_ms;
    cpp_cfg.max_resends = c_cfg->max_resends;
    cpp_cfg.fast_resend_count = c_cfg->fast_resend_count;
    cpp_cfg.max_pending_unreliable_commands = c_cfg->max_pending_unreliable_commands;
    cpp_cfg.max_payload_size = c_cfg->max_payload_size;
    cpp_cfg.max_messages_per_second = c_cfg->max_messages_per_second;
    cpp_cfg.max_dispatches_per_tick = c_cfg->max_dispatches_per_tick;
}

// C interface

extern "C" {
void luxon_enet_peer_config_init_default(luxon_enet_peer_config_t *config) {
    if (!config)
        return;
    luxon::enet::EnetPeerConfig def;
    config->mtu = def.mtu;
    config->channel_count = def.channel_count;
    config->crc_enabled = def.crc_enabled;
    config->time_base = def.time_base;
    config->time_ping_interval_ms = def.time_ping_interval_ms;
    config->disconnect_timeout_ms = def.disconnect_timeout_ms;
    config->max_resends = def.max_resends;
    config->fast_resend_count = def.fast_resend_count;
    config->max_pending_unreliable_commands = def.max_pending_unreliable_commands;
    config->max_payload_size = def.max_payload_size;
    config->max_messages_per_second = def.max_messages_per_second;
    config->max_dispatches_per_tick = def.max_dispatches_per_tick;
}

luxon_enet_peer_t *luxon_enet_peer_create(const luxon_enet_peer_config_t *config) {
    try {
        luxon::enet::EnetPeerConfig cpp_cfg;
        cpp_config_from_c(cpp_cfg, config);

        auto p = new luxon_enet_peer();
        p->client_sock = std::make_unique<luxon::enet::UdpSocket>();

#ifdef LUXON_ENET_ENABLE_METRICS
        // In this C binding, client peers have independent dummy metrics when used outside a server.
        // If they need to be aggregated, it requires a larger C API redesign for metrics sharing.
        static luxon::enet::Metrics dummy_metrics;
        p->peer = std::make_shared<luxon::enet::EnetPeer>(cpp_cfg, dummy_metrics);
#else
        p->peer = std::make_shared<luxon::enet::EnetPeer>(cpp_cfg);
#endif

        p->is_server_managed = false;
        p->hook_callbacks();
        return p;
    } catch (...) {
        return nullptr;
    }
}

void luxon_enet_peer_destroy(luxon_enet_peer_t *peer) {
    if (!peer)
        return;
    try {
        if (!peer->is_server_managed && peer->peer) {
            peer->peer->disconnect(true);
        }
        delete peer;
    } catch (...) {
    }
}

void luxon_enet_peer_set_callbacks(luxon_enet_peer_t *peer, luxon_enet_peer_on_state_changed_cb on_state, luxon_enet_peer_on_payload_cb on_payload,
                                   luxon_enet_peer_on_log_cb on_log, void *user_data) {
    if (!peer)
        return;
    peer->user_data = user_data;
    peer->on_state = on_state;
    peer->on_payload = on_payload;
    peer->on_log = on_log;
}

bool luxon_enet_peer_connect(luxon_enet_peer_t *peer, const char *host, uint16_t port) {
    if (!peer || peer->is_server_managed || !peer->client_sock || !peer->peer)
        return false;
    try {
        // Automatically bind ephemeral port before connecting
        peer->client_sock->bind_any(0, true);
        return peer->peer->connect(*(peer->client_sock), host, port);
    } catch (...) {
        return false;
    }
}

void luxon_enet_peer_disconnect(luxon_enet_peer_t *peer, bool noflush) {
    if (!peer || !peer->peer)
        return;
    try {
        peer->peer->disconnect(noflush);
    } catch (...) {
    }
}

bool luxon_enet_peer_send_payload(luxon_enet_peer_t *peer, const uint8_t *payload, size_t size, const luxon_enet_send_options_t *options) {
    if (!peer || !peer->peer || !options)
        return false;
    try {
        luxon::enet::EnetSendOptions cpp_opts;
        cpp_opts.channel = options->channel;
        cpp_opts.mode = static_cast<luxon::enet::EnetDeliveryMode>(options->mode);

        return peer->peer->send_payload(std::span<const uint8_t>(payload, size), cpp_opts);
    } catch (...) {
        return false;
    }
}

void luxon_enet_peer_service(luxon_enet_peer_t *peer) {
    if (!peer || peer->is_server_managed || !peer->client_sock || !peer->client_sock->is_open())
        return;
    try {
        peer->peer->service();

        // Pump socket data into peer
        uint8_t buffer[4096];
        luxon::enet::EnetEndpoint from;

        peer->client_sock->set_nonblocking(true);
        size_t bytes = peer->client_sock->recv_from(buffer, sizeof(buffer), from);

        while (bytes > 0) {
            peer->peer->handle_incoming_datagram(std::span<const uint8_t>(buffer, bytes));
            bytes = peer->client_sock->recv_from(buffer, sizeof(buffer), from);
        }

        // Dispatch sequenced packets to user callbacks
        while (peer->peer->dispatch_one()) {
        }
    } catch (...) {
    }
}

int16_t luxon_enet_peer_get_id(const luxon_enet_peer_t *peer) { return (peer && peer->peer) ? peer->peer->peer_id() : -1; }

size_t luxon_enet_peer_get_remote_ip(const luxon_enet_peer_t *peer, char *out_ip, size_t max_len) {
    if (max_len < 1)
        return 0;
    out_ip[0] = '\0';

    if (!peer || !peer->peer)
        return 0;

    const auto& ep = peer->peer->remote_endpoint();
    if (!ep)
        return 0;

    const std::string ip_str = ep->get_ip();
    std::string_view ip_view(ip_str);

    const size_t copy_len = std::min(ip_view.size(), max_len - 1);
    ip_view.copy(out_ip, copy_len);
    out_ip[copy_len] = '\0';
    return ip_view.size();
}

uint16_t luxon_enet_peer_get_remote_port(const luxon_enet_peer_t *peer) {
    if (!peer || !peer->peer)
        return 0;

    const auto& ep = peer->peer->remote_endpoint();
    if (!ep)
        return 0;

    return ep->get_port();
}

luxon_enet_connection_state_t luxon_enet_peer_get_state(const luxon_enet_peer_t *peer) {
    return (peer && peer->peer) ? static_cast<luxon_enet_connection_state_t>(peer->peer->state()) : LUXON_ENET_CONN_DISCONNECTED;
}

int luxon_enet_peer_get_rtt(const luxon_enet_peer_t *peer) { return (peer && peer->peer) ? peer->peer->round_trip_time() : 0; }

int luxon_enet_peer_get_rtt_variance(const luxon_enet_peer_t *peer) { return (peer && peer->peer) ? peer->peer->round_trip_variance() : 0; }

uint64_t luxon_enet_peer_get_bytes_in(const luxon_enet_peer_t *peer) { return (peer && peer->peer) ? peer->peer->bytes_in() : 0; }

uint64_t luxon_enet_peer_get_bytes_out(const luxon_enet_peer_t *peer) { return (peer && peer->peer) ? peer->peer->bytes_out() : 0; }

luxon_enet_server_t *luxon_enet_server_create(const luxon_enet_peer_config_t *config) {
    try {
        luxon::enet::EnetPeerConfig cpp_cfg;
        cpp_config_from_c(cpp_cfg, config);

        auto s = new luxon_enet_server();

#ifdef LUXON_ENET_ENABLE_METRICS
        s->server = std::make_unique<luxon::enet::EnetServer>(cpp_cfg, s->metrics);
#else
        s->server = std::make_unique<luxon::enet::EnetServer>(cpp_cfg);
#endif

        s->server->on_peer_connected = [s](std::shared_ptr<luxon::enet::EnetPeer> cpp_peer) {
            if (s->on_connect) {
                auto wrap = new luxon_enet_peer();
                wrap->peer = cpp_peer;
                wrap->is_server_managed = true;
                wrap->hook_callbacks();

                s->on_connect(s, wrap, s->user_data);
            }
        };

        return s;
    } catch (...) {
        return nullptr;
    }
}

void luxon_enet_server_destroy(luxon_enet_server_t *server) {
    if (!server)
        return;
    try {
        delete server;
    } catch (...) {
    }
}

bool luxon_enet_server_bind(luxon_enet_server_t *server, uint16_t port, bool ipv6) {
    if (!server || !server->server)
        return false;
    try {
        return server->server->bind(port, ipv6);
    } catch (...) {
        return false;
    }
}

void luxon_enet_server_set_on_peer_connected(luxon_enet_server_t *server, luxon_enet_server_on_peer_connected_cb cb, void *user_data) {
    if (!server)
        return;
    server->user_data = user_data;
    server->on_connect = cb;
}

void luxon_enet_server_service(luxon_enet_server_t *server, uint32_t *timeout_us) {
    if (!server || !server->server)
        return;
    try {
        uint32_t c_timeout = timeout_us ? *timeout_us : 0;
        server->server->service(c_timeout);
        if (timeout_us) {
            *timeout_us = c_timeout;
        }
    } catch (...) {
    }
}
} // extern "C"
