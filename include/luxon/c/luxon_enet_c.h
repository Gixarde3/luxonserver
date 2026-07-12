// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Enums & Structs
 */

typedef enum {
    LUXON_ENET_CONN_DISCONNECTED = 0,
    LUXON_ENET_CONN_CONNECTING,
    LUXON_ENET_CONN_CONNECTED,
    LUXON_ENET_CONN_DISCONNECTING,
    LUXON_ENET_CONN_STALE
} luxon_enet_connection_state_t;

typedef enum {
    LUXON_ENET_DELIVERY_UNRELIABLE = 0,
    LUXON_ENET_DELIVERY_RELIABLE = 1,
    LUXON_ENET_DELIVERY_UNRELIABLE_UNSEQUENCED = 2,
    LUXON_ENET_DELIVERY_RELIABLE_UNSEQUENCED = 3
} luxon_enet_delivery_mode_t;

typedef enum { LUXON_ENET_LOG_WARNING = 0, LUXON_ENET_LOG_ERROR = 1 } luxon_enet_log_level_t;

typedef struct {
    uint16_t mtu;
    uint8_t channel_count;
    bool crc_enabled;
    int time_base;
    int time_ping_interval_ms;
    int disconnect_timeout_ms;
    uint8_t max_resends;
    uint8_t fast_resend_count;
    int max_pending_unreliable_commands;
    size_t max_payload_size;
    uint32_t max_messages_per_second;
    uint8_t max_dispatches_per_tick;
} luxon_enet_peer_config_t;

typedef struct {
    uint8_t channel;
    luxon_enet_delivery_mode_t mode;
} luxon_enet_send_options_t;

/*
 * Opaque Handles
 */

typedef struct luxon_enet_server luxon_enet_server_t;
typedef struct luxon_enet_peer luxon_enet_peer_t;

/*
 * Callbacks
 */

typedef void (*luxon_enet_peer_on_state_changed_cb)(luxon_enet_peer_t *peer, luxon_enet_connection_state_t state, void *user_data);
typedef void (*luxon_enet_peer_on_payload_cb)(luxon_enet_peer_t *peer, const uint8_t *payload, size_t size, void *user_data);
typedef void (*luxon_enet_peer_on_log_cb)(luxon_enet_peer_t *peer, luxon_enet_log_level_t level, const char *msg, void *user_data);

typedef void (*luxon_enet_server_on_peer_connected_cb)(luxon_enet_server_t *server, luxon_enet_peer_t *peer, void *user_data);

/*
 * Config Utilities
 */

// Fills provided config structure with ENet defaults
void luxon_enet_peer_config_init_default(luxon_enet_peer_config_t *config);

/*
 * Peer API (Client / Remote Server Peer)
 */

// Creates standalone client peer
// Must be deletd later with luxon_enet_peer_destroy()
luxon_enet_peer_t *luxon_enet_peer_create(const luxon_enet_peer_config_t *config);

// Destroys peer handle. If standalone client, disconnects
// If server managed, just releases handle
void luxon_enet_peer_destroy(luxon_enet_peer_t *peer);

// Sets event callbacks for peer
void luxon_enet_peer_set_callbacks(luxon_enet_peer_t *peer, luxon_enet_peer_on_state_changed_cb on_state, luxon_enet_peer_on_payload_cb on_payload,
                                   luxon_enet_peer_on_log_cb on_log, void *user_data);

// Connects to remote host (client only)
bool luxon_enet_peer_connect(luxon_enet_peer_t *peer, const char *host, uint16_t port);

// Disconnects peer
void luxon_enet_peer_disconnect(luxon_enet_peer_t *peer, bool noflush);

// Sends application payload
bool luxon_enet_peer_send_payload(luxon_enet_peer_t *peer, const uint8_t *payload, size_t size, const luxon_enet_send_options_t *options);

// Serves network events (receiving packets and doing callbacks) for standalone clients
void luxon_enet_peer_service(luxon_enet_peer_t *peer);

// Accessors
int16_t luxon_enet_peer_get_id(const luxon_enet_peer_t *peer);
luxon_enet_connection_state_t luxon_enet_peer_get_state(const luxon_enet_peer_t *peer);
int luxon_enet_peer_get_rtt(const luxon_enet_peer_t *peer);
int luxon_enet_peer_get_rtt_variance(const luxon_enet_peer_t *peer);
uint64_t luxon_enet_peer_get_bytes_in(const luxon_enet_peer_t *peer);
uint64_t luxon_enet_peer_get_bytes_out(const luxon_enet_peer_t *peer);

/*
 * Server API
 */

// Creates new ENet server that binds to port and listens for peers
luxon_enet_server_t *luxon_enet_server_create(const luxon_enet_peer_config_t *config);

// Destroys server and kicks all peers
void luxon_enet_server_destroy(luxon_enet_server_t *server);

// Binds server socket to specific port. Returns false on failure
bool luxon_enet_server_bind(luxon_enet_server_t *server, uint16_t port, bool ipv6);

// Sets callback invoked when new client establishes an ENet connection
// Must be deletd later with luxon_enet_peer_destroy()
void luxon_enet_server_set_on_peer_connected(luxon_enet_server_t *server, luxon_enet_server_on_peer_connected_cb cb, void *user_data);

// Processes socket I/O, ticks timeouts, and dispatches callbacks
// `timeout_us` specifies how long function can block
void luxon_enet_server_service(luxon_enet_server_t *server, uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
