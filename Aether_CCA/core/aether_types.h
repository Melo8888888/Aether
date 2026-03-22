#ifndef AETHER_TYPES_H
#define AETHER_TYPES_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#endif

#ifndef __KERNEL__
typedef uint64_t u64;
typedef uint32_t u32;
typedef int64_t s64;
typedef int32_t s32;
#endif

struct aether_params {
    size_t gamma;
    double alpha;
    size_t k;
    double c_threshold;
    double dwell_rtt_ratio_threshold;
    size_t consecutive_events_threshold;
};

#define AETHER_DEFAULT_PARAMS ((struct aether_params){ \
    .gamma = 5, \
    .alpha = 0.6, \
    .k = 12, \
    .c_threshold = 3.5, \
    .dwell_rtt_ratio_threshold = 0.5, \
    .consecutive_events_threshold = 5 \
})

enum aether_bottleneck_state {
    AETHER_BOTTLENECK_UPLINK,
    AETHER_BOTTLENECK_INTERNET
};

enum aether_congestion_state {
    AETHER_STATE_NORMAL,
    AETHER_STATE_DRAINING,
    AETHER_STATE_RECOVERING
};

struct aether_dwell_sample {
    u64 timestamp_ms;
    double dwell_time_ms;
    size_t queue_length;
    size_t packet_size_bytes;
};

struct aether_packet_meta {
    u64 seq_num;
    u64 enqueue_time_ms;
    u64 dequeue_time_ms;
    size_t packet_size;
    bool departed;
};

struct aether_tcp_state {
    double cwnd;
    double pacing_rate;
    double rtt_ms;
    double min_rtt_ms;
    double ave_rtt_ms;
    u64 bytes_acked;
};

struct aether_decision {
    double new_cwnd;
    double new_pacing_rate;
    enum aether_bottleneck_state bottleneck;
    enum aether_congestion_state congestion;
    double estimated_capacity;
    bool use_default_cca;
};

struct aether_stats {
    u64 packets_monitored;
    double avg_dwell_time_ms;
    double max_dwell_time_ms;
    u64 bottleneck_transitions;
    u64 sudden_degradation_events;
    u64 capacity_probes;
};

#endif
