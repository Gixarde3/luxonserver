// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "enet_protocol.hpp"
#include "sliding_flat_map.hpp"
#ifdef LUXON_ENET_ENABLE_METRICS
#include "enet_metrics.hpp"
#endif

#include <vector>
#include <array>
#include <queue>
#include <unordered_map>
#include <set>
#include <deque>
#include <optional>
#include <functional>
#include <string>
#include <memory>
#include <cstdint>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace luxon {
namespace enet {
enum class LogLevel { Warning, Error };

enum class EnetConnectionState { Disconnected, Connecting, Connected, Disconnecting, Stale };

enum class EnetDeliveryMode { Unreliable, Reliable, UnreliableUnsequenced, ReliableUnsequenced };

struct EnetSendOptions {
    uint8_t channel = 0;
    EnetDeliveryMode mode = EnetDeliveryMode::Reliable;
};

struct EnetPeerConfig {
    uint16_t mtu = 1200;
    uint8_t channel_count = 2;
    bool crc_enabled = false;
    int time_base = 0;

    // Approximate measured values
    int time_ping_interval_ms = 1000;
    int disconnect_timeout_ms = 10000;

    uint8_t max_resends = 7; // Allows about 20 seconds of resends
    uint8_t fast_resend_count = 0;
    int max_pending_unreliable_commands = 0;

    // Parse incoming connect command and extract configuration
    void apply_connect_command(const EnetCommand& cmd) {
        if (cmd.header.command_type == EnetCommandType::Connect && cmd.get_payload_size() >= 12) {
            const auto payload = cmd.get_payload();
            mtu = static_cast<uint16_t>((payload[2] << 8) | payload[3]);
            channel_count = payload[11];
        }
    }
};

EnetDeliveryMode FlagsToEnetDeliveryMode(uint8_t flags);
inline static EnetDeliveryMode EnetCommandGetEnetDeliveryMode(const EnetCommand& cmd) { return FlagsToEnetDeliveryMode(cmd.header.flags); }

struct EnetEndpoint {
    sockaddr_storage addr{};
    socklen_t len = 0;

    static std::optional<EnetEndpoint> from(const char *host, uint16_t port);

    bool operator==(const EnetEndpoint& o) const;
    std::string to_string() const;
};

struct EnetEndpointHash {
    std::size_t operator()(const EnetEndpoint& ep) const noexcept;
};

#ifdef _WIN32
using SocketType = SOCKET;
#else
using SocketType = int;
#endif

// Low-level UDP socket wrapper (blocking recv in poll loop)
class UdpSocket {
public:
    UdpSocket();
    UdpSocket(SocketType native_handle);
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool bind_any(uint16_t port, bool ipv6 = false);
    bool connect_to(const std::string& host, uint16_t port);

    static std::optional<EnetEndpoint> lookup_hostname(const char *hostname, uint16_t port = 0) noexcept;

    bool send_stun_binding_request(const EnetEndpoint& to);
    std::optional<EnetEndpoint> parse_stun_binding_response(DatagramView datagram);

    bool is_open() const {
#ifdef _WIN32
        return sock_ != INVALID_SOCKET;
#else
        return sock_ >= 0;
#endif
    }
    SocketType native_handle() const { return sock_; }

    bool send_to(const uint8_t *data, size_t len, const EnetEndpoint& to);
    bool send_connected(const uint8_t *data, size_t len);

    // Returns (len, from). len==0 means no data (non-blocking) or error
    size_t recv_from(uint8_t *buf, size_t cap, EnetEndpoint& from);

    void set_nonblocking(bool nb);
    void close();

private:
#if defined(_WIN32)
    SOCKET sock_ = INVALID_SOCKET;
#else
    int sock_ = -1;
#endif
    bool owning_ = true;
    bool connected_ = false;

    std::array<uint8_t, 12> stun_transaction_id_ = {};
    bool stun_request_pending_ = false;
};

// Internal command with resend metadata
struct EnetOutCommand {
    EnetCommand cmd;

    int command_sent_time = 0;
    uint8_t command_sent_count = 0;

    int round_trip_timeout = 0;
    int timeout_time = 0;
};

// Channel state
class EnetChannel {
public:
    explicit EnetChannel(uint8_t channel);

    uint8_t channel_number() const { return channel_; }

    // outgoing
    std::queue<EnetOutCommand> outgoing_reliable;
    std::queue<EnetCommand> outgoing_unreliable;

    // incoming storage
    sliding_flat_map<uint32_t, EnetCommand, 128> incoming_reliable;           // by reliable_seq
    sliding_flat_map<uint32_t, EnetCommand, 256> incoming_unreliable;         // by unreliable_seq
    std::queue<EnetCommand> incoming_unsequenced;                             // ready to dispatch (also reassembled frags)
    sliding_flat_map<uint32_t, EnetCommand, 64> incoming_unsequenced_frags;   // reliable_seq -> fragment cmd for unsequenced fragments

    // seq numbers
    uint32_t incoming_reliable_seq = 0;
    uint32_t incoming_unreliable_seq = 0;
    uint32_t outgoing_reliable_seq = 0;
    uint32_t outgoing_unreliable_seq = 0;
    uint32_t outgoing_reliable_unsequenced_seq = 0;

    // Reliable-unsequenced completion tracking
    uint32_t reliable_unsequenced_completely_received = 0;
    std::set<uint32_t> reliable_unsequenced_received;

    bool queue_incoming_reliable_unsequenced(const EnetCommand& cmd);
    bool try_get_fragment(uint32_t reliable_seq, bool sequenced, EnetCommand& out) const;
    void remove_fragment(uint32_t reliable_seq, bool sequenced);

    void sync_reliable_window();
    void sync_unreliable_window();
    void sync_reliable_unsequenced_fragment_window();

private:
    uint8_t channel_;
};

// A single peer (client-side or server-side) speaking ENet
class EnetPeer {
public:
    explicit EnetPeer(EnetPeerConfig cfg
#ifdef LUXON_ENET_ENABLE_METRICS
                      ,
                      Metrics& metrics
#endif
    );
    ~EnetPeer();

    // Client mode
    bool use(UdpSocket& sock);
    bool connect(UdpSocket& sock, const std::string& host, uint16_t port);
    void disconnect(bool noflush = false);

    // Server-side mode attach: use endpoint + assigned peer_id + challenge from client
    void attach_server_side(UdpSocket& sock, const EnetEndpoint& remote, int16_t assigned_peer_id, uint32_t challenge);

    EnetConnectionState state() const { return state_; }
    int16_t peer_id() const { return peer_id_; }
    uint32_t challenge() const { return challenge_; }

    // Enqueue application payload to send (raw message bytes)
    // This API is transport-level: payload is the command payload (already contains message header like 0xF3 0x02 etc)
    bool send_payload(DatagramView payload, const EnetSendOptions& opt);

    // Process timers + resend + ping + send outgoing datagrams
    bool service();

    // Feed datagrams received from socket. This parses, checks challenge, handles ACKs, handles sequencing and fragmentation
    // and queues payloads for dispatch
    void handle_incoming_datagram(std::span<const uint8_t> datagram, bool count_io_metrics = true);

    // Feed packet received from socket. This checks challenge, handles ACKs, handles sequencing and fragmentation
    // and queues payloads for dispatch
    void handle_incoming_packet(const EnetPacketHeader& hdr, std::span<EnetCommand> cmds, size_t datagram_size);

    // Dispatch exactly ONE queued incoming payload command
    // Returns true if something was dispatched
    bool dispatch_one();

    // Stats
    int round_trip_time() const { return rtt_; }
    int round_trip_variance() const { return rtt_var_; }
    uint64_t bytes_out() const { return bytes_out_; }
    uint64_t bytes_in() const { return bytes_in_; }
    int packet_loss_by_crc() const { return packet_loss_by_crc_; }
    int packet_loss_by_challenge() const { return packet_loss_by_challenge_; }

    // For server side
    const std::optional<EnetEndpoint>& remote_endpoint() const { return remote_; }

    // Time related functions
    int get_server_time() const { return now_ms(); }
    static int create_time_base();
    void sync_local_time_to_remote_dynamic(const EnetPeer& remote);

    // Get underlaying socket
    UdpSocket& socket() { return *sock_; }
    const UdpSocket& socket() const { return *sock_; }

    // Callbacks (optional)
    void reset_callbacks() {
        on_state_changed = nullptr;
        on_payload_command = nullptr;
        on_log_message = nullptr;
    }

    std::function<void(EnetConnectionState)> on_state_changed;
    std::function<void(EnetCommand&&)> on_payload_command;
    std::function<void(LogLevel, std::string_view)> on_log_message;

private:
    void set_state(EnetConnectionState new_state);

    // Core protocol helpers
    void send_connect();
    int now_ms() const;

    size_t calculate_initial_offset() const; // 12 or 16 (CRC)
    size_t calculate_buffer_len() const;     // mtu (plain)
    size_t get_fragment_length();            // mtu - 12 - 36 (plain)

    EnetChannel& channel(uint8_t ch);
    const EnetChannel& channel(uint8_t ch) const;

    void queue_outgoing_ack(const EnetCommand& received_reliable_cmd, uint32_t sent_time);
    void queue_sent_reliable(EnetOutCommand& outcmd);
    void queue_outgoing_reliable(EnetOutCommand outcmd);
    void queue_outgoing_unreliable(EnetCommand cmd);

    bool send_outgoing_commands();
    bool send_acks_only();

    bool are_reliable_commands_in_transit() const;

    // Incoming command execution (ACK, connect, verifyconnect, disconnect, ping, payload, fragments...)
    void execute_command(const EnetCommand& cmd);

    bool queue_incoming_command(const EnetCommand& cmd);

    // Ack handling
    std::optional<EnetOutCommand> remove_sent_reliable(uint32_t ack_seq, uint8_t channel_id, bool is_unsequenced);

    // Fragment reassembly
    void handle_fragment(const EnetCommand& fragment_cmd);

    // RTT updates
    void update_rtt(int last_rtt);

    // Datagram building/sending
    bool flush_send_queue(bool only_acks);
    bool send_datagram(DatagramView datagram);

private:
#ifdef LUXON_ENET_ENABLE_METRICS
    Metrics& metrics_;
#endif

    EnetPeerConfig cfg_;
    UdpSocket *sock_ = nullptr;

    // Client remote endpoint is implicit via connected socket
    // Server-side stores explicit endpoint to sendto()
    std::optional<EnetEndpoint> remote_;

    EnetConnectionState state_ = EnetConnectionState::Disconnected;

    int16_t peer_id_ = -1;
    uint32_t challenge_ = 0;

    // Header timestamps
    int time_base_ = 0;
    int time_int_ = 0;

    // Ping/ack timers
    int timeout_int_ = 0;
    int time_last_ack_receive_ = 0;
    int time_last_send_ack_ = 0;
    int time_last_send_outgoing_ = 0;

    // RTT stats
    int rtt_ = 200;
    int rtt_var_ = 5;
    int last_rtt_ = 0;

    // Packet loss counters
    int packet_loss_by_crc_ = 0;
    int packet_loss_by_challenge_ = 0;

    // Bytes
    uint64_t bytes_out_ = 0;
    uint64_t bytes_in_ = 0;

    // outgoing buffers/queues
    uint8_t outgoing_command_count_ = 0;
    std::deque<std::array<uint8_t, 20>> outgoing_ack_pool_; // each element is exactly 20 bytes (CmdSizeAck)

    // sent reliable list (for retransmit)
    std::vector<EnetOutCommand> sent_reliable_;

    // Enet channels (+ control channel at index channel_count)
    std::vector<std::unique_ptr<EnetChannel>> channels_;
    static constexpr uint8_t ControlChannel = 0xFF;

    // Unsequenced window
    std::array<uint32_t, 4> unsequenced_window_ = {};
    uint32_t outgoing_unsequenced_group_ = 0;
    uint32_t incoming_unsequenced_group_ = 0;

    // Fragment length cache
    size_t fragment_length_ = 0;
    uint16_t fragment_length_mtu_ = 0;

    // Dispatch queue: commands ready to deliver in order
    std::queue<EnetCommand> dispatch_queue_;

    // Datagram queue: datagrams to be sent later
    std::queue<std::pair<DatagramBuffer, size_t>> datagram_queue_;
};

// Server that accepts peers and routes datagrams to them
class EnetServer {
public:
    explicit EnetServer(EnetPeerConfig cfg
#ifdef LUXON_ENET_ENABLE_METRICS
                        ,
                        Metrics& metrics
#endif
    );

    bool bind(uint16_t port, bool ipv6 = false);
    void service() {
        service_self();
        service_peers();
    }
    void service_self();
    bool service_peers();

    // Called when a new peer is created (after receiving EnetCommandType::Connect)
    std::function<void(std::shared_ptr<EnetPeer>)> on_peer_connected;

    // Called when STUN binding is complete
    std::function<void(EnetEndpoint&&)> on_stun_bind;

    // Find peer by assigned peer_id
    std::shared_ptr<EnetPeer> find_peer(int16_t peer_id) const;

    // Delete peer by shared pointer, used to finalize disconnect
    void remove_peer(std::shared_ptr<EnetPeer> peer);

    // Request STUN binding
    bool request_stun_binding(const char *server_hostname, uint16_t server_port);

    // Keep STUN binding alive
    bool keepalive_stun_binding();

    // Get endpoint of used STUN server
    const EnetEndpoint& stun_server_endpoint() { return stun_ep_; }

    // Get UDP socket
    UdpSocket& socket() { return sock_; }
    const UdpSocket& socket() const { return sock_; }

    // Get native UDP socket
    SocketType native_handle() const { return sock_.native_handle(); }

private:
#ifdef LUXON_ENET_ENABLE_METRICS
    Metrics& metrics_;
#endif

    EnetPeerConfig cfg_;
    UdpSocket sock_;

    int16_t next_peer_id_ = 1;

    // endpoint->peer mapping
    std::unordered_map<EnetEndpoint, std::shared_ptr<EnetPeer>, EnetEndpointHash> peers_by_ep_;
    std::unordered_map<int16_t, std::shared_ptr<EnetPeer>> peers_by_id_;

    // STUN endpoint
    EnetEndpoint stun_ep_{};
};
} // namespace enet
} // namespace luxon
