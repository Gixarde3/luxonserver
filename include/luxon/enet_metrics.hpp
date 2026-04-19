#pragma once

#include <cstdint>

// Inspired by: https://doc.photonengine.com/server/v4/performance/photon-counters-list

namespace luxon {
namespace enet {
using PerformanceCounterInt = uint64_t;

struct RateCounter {
    PerformanceCounterInt total{0};
    PerformanceCounterInt last_total{0};
    PerformanceCounterInt per_second{0};

    inline void add(PerformanceCounterInt amount = 1) noexcept { total += amount; }

    inline void tick(double dt_seconds) noexcept {
        if (dt_seconds > 0.0)
            per_second = static_cast<PerformanceCounterInt>((total - last_total) / dt_seconds);
        last_total = total;
    }
};

struct GaugeCounter {
    PerformanceCounterInt current{0};

    inline void add(PerformanceCounterInt amount = 1) noexcept { current += amount; }
    inline void sub(PerformanceCounterInt amount = 1) noexcept {
        if (current >= amount)
            current -= amount;
        else
            current = 0;
    }
};

struct FlowCounter {
    PerformanceCounterInt total_added{0};
    PerformanceCounterInt total_removed{0};

    PerformanceCounterInt last_total_added{0};
    PerformanceCounterInt last_total_removed{0};

    PerformanceCounterInt added_per_sec{0};
    PerformanceCounterInt removed_per_sec{0};

    inline void add(PerformanceCounterInt amount = 1) noexcept { total_added += amount; }
    inline void sub(PerformanceCounterInt amount = 1) noexcept { total_removed += amount; }

    inline PerformanceCounterInt current() const noexcept { return total_added - total_removed; }
    inline PerformanceCounterInt all_time() const noexcept { return total_added; }

    inline void tick() noexcept {
        added_per_sec = total_added - last_total_added;
        removed_per_sec = total_removed - last_total_removed;

        last_total_added = total_added;
        last_total_removed = total_removed;
    }
};

struct alignas(64) Metrics {
    // Global Counters
    struct {
        RateCounter bytes_in;
        RateCounter bytes_out;
        RateCounter messages_in;
        RateCounter messages_out;
        GaugeCounter connections_active;

        FlowCounter peers;
        RateCounter disconnected_peers;
        RateCounter disconnected_peers_c; // Client
        RateCounter disconnected_peers_s; // Server
        RateCounter disconnected_peers_t; // Timeout
    } global;

    // UDP Counters
    struct {
        RateCounter datagrams_in;
        RateCounter datagrams_out;
    } udp;

    // ENet Counters
    struct {
        RateCounter datagram_validation_failures;
        RateCounter commands_in;
        RateCounter commands_out;
        RateCounter commands_out_throttled;

        RateCounter reliable_commands_in;
        RateCounter reliable_commands_out;
        RateCounter reliable_commands_in_dropped;
        RateCounter reliable_commands_out_resent;

        RateCounter unreliable_commands_in;
        RateCounter unreliable_commands_out;
        RateCounter unreliable_commands_in_dropped;

        RateCounter acknowledgements_in;
        RateCounter acknowledgements_out;
        RateCounter pings_in;
        RateCounter pings_out;
        RateCounter timeout_disconnects;

        // RateCounter transmit_rate_limit_bytes_queued;
        // RateCounter transmit_rate_limit_bytes_discarded;
        // RateCounter transmit_rate_limit_messages_queued;
        // RateCounter transmit_rate_limit_messages_discarded;

        // RateCounter transmit_window_limit_bytes_queued;
        // RateCounter transmit_window_limit_bytes_discarded;
        // RateCounter transmit_window_limit_messages_queued;
        // RateCounter transmit_window_limit_messages_discarded;
    } enet;
};
} // namespace enet
} // namespace luxon
