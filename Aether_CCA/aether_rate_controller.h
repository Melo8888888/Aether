#ifndef AETHER_RATE_CONTROLLER_H
#define AETHER_RATE_CONTROLLER_H

#include "aether_types.h"

struct aether_rate_controller {
    struct aether_params params;
    double estimated_capacity;
    enum aether_congestion_state congestion_state;
    size_t probe_count;
    double frozen_cwnd;
    double frozen_pacing_rate;
    u64 drain_start_time;
    u64 draining_end_time;
    size_t drain_queue_size;
};

void aether_rate_controller_init(struct aether_rate_controller* rc, const struct aether_params* params);

double aether_rate_controller_estimate_capacity(struct aether_rate_controller* rc, const struct aether_dwell_sample* samples, size_t count);
double aether_rate_controller_update_cwnd(struct aether_rate_controller* rc, double current_cwnd, double current_rate, double dwell_time, double rtt);
double aether_rate_controller_calculate_pacing_rate(const struct aether_rate_controller* rc, double cwnd, double rtt);

void aether_rate_controller_handle_sudden_degradation(struct aether_rate_controller* rc, double current_cwnd, double current_rate, size_t queue_size, u64 timestamp);
double aether_rate_controller_complete_drain(struct aether_rate_controller* rc, u64 drain_end_time);

size_t aether_rate_controller_get_probe_packet_count(const struct aether_rate_controller* rc);
size_t aether_rate_controller_update_probe_count(struct aether_rate_controller* rc, bool queue_buildup_observed);
void aether_rate_controller_complete_recovery(struct aether_rate_controller* rc);
void aether_rate_controller_reset(struct aether_rate_controller* rc);

#endif
