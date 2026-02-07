#ifndef AETHER_TYPES_HH
#define AETHER_TYPES_HH

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace aether {

struct AetherParams {
    size_t gamma = 5;
    double alpha = 0.6;
    size_t K = 12;
    double C_threshold = 3.5;
    double dwell_rtt_ratio_threshold = 0.5;
    size_t consecutive_events_threshold = 5;
};

enum class BottleneckState {
    UPLINK_BOTTLENECK,
    INTERNET_BOTTLENECK
};

enum class CongestionState {
    NORMAL,
    DRAINING,
    RECOVERING
};

struct DwellSample {
    uint64_t timestamp_ms;
    double dwell_time_ms;
    size_t queue_length;
    size_t packet_size_bytes;

    DwellSample() = default;
    DwellSample(uint64_t ts, double dwell, size_t q_len, size_t p_size)
        : timestamp_ms(ts), dwell_time_ms(dwell), 
          queue_length(q_len), packet_size_bytes(p_size) {}
};

struct PacketMeta {
    uint64_t seq_num;
    uint64_t enqueue_time_ms;
    uint64_t dequeue_time_ms;
    size_t packet_size;
    bool departed;

    PacketMeta() : seq_num(0), enqueue_time_ms(0), dequeue_time_ms(0), 
                   packet_size(0), departed(false) {}
    
    PacketMeta(uint64_t seq, uint64_t enq_time, size_t size)
        : seq_num(seq), enqueue_time_ms(enq_time), dequeue_time_ms(0),
          packet_size(size), departed(false) {}

    double dwell_time_ms() const {
        if (!departed || dequeue_time_ms <= enqueue_time_ms) return 0;
        return static_cast<double>(dequeue_time_ms - enqueue_time_ms);
    }
};

struct TCPState {
    double cwnd;
    double pacing_rate;
    double rtt_ms;
    double min_rtt_ms;
    double ave_rtt_ms;
    uint64_t bytes_acked;
    
    TCPState() : cwnd(10 * 1460), pacing_rate(0), 
                 rtt_ms(0), min_rtt_ms(1e9), ave_rtt_ms(0), 
                 bytes_acked(0) {}
};

struct AetherDecision {
    double new_cwnd;
    double new_pacing_rate;
    BottleneckState bottleneck;
    CongestionState congestion;
    double estimated_capacity;
    bool use_default_cca;
};

struct AetherStats {
    uint64_t packets_monitored = 0;
    double avg_dwell_time_ms = 0;
    double max_dwell_time_ms = 0;
    uint64_t bottleneck_transitions = 0;
    uint64_t sudden_degradation_events = 0;
    uint64_t capacity_probes = 0;
    
    void reset() {
        packets_monitored = 0;
        avg_dwell_time_ms = 0;
        max_dwell_time_ms = 0;
        bottleneck_transitions = 0;
        sudden_degradation_events = 0;
        capacity_probes = 0;
    }
};

} // namespace aether

#endif // AETHER_TYPES_HH
