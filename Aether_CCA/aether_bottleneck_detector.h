#ifndef AETHER_BOTTLENECK_DETECTOR_H
#define AETHER_BOTTLENECK_DETECTOR_H

#include "aether_types.h"

struct aether_bottleneck_detector {
    struct aether_params params;
    enum aether_bottleneck_state current_state;
    size_t consecutive_negative_count;
    size_t consecutive_positive_count;
    u64 transition_count;
    
    void (*state_change_callback)(enum aether_bottleneck_state from, enum aether_bottleneck_state to, void* ctx);
    void* callback_ctx;
};

void aether_bottleneck_detector_init(struct aether_bottleneck_detector* det, const struct aether_params* params);

enum aether_bottleneck_state aether_bottleneck_detector_update(
    struct aether_bottleneck_detector* det,
    double delta_d_k, double dwell_time,
    double rtt_e2e, double ave_rtt);

void aether_bottleneck_detector_force_state(struct aether_bottleneck_detector* det, enum aether_bottleneck_state state);
void aether_bottleneck_detector_reset(struct aether_bottleneck_detector* det);

const char* aether_bottleneck_state_to_string(enum aether_bottleneck_state state);

#endif
