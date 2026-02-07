#include "aether_bottleneck_detector.h"
#include <string.h>

void aether_bottleneck_detector_init(struct aether_bottleneck_detector* det, const struct aether_params* params) {
    if (!det) return;
    memset(det, 0, sizeof(struct aether_bottleneck_detector));
    if (params) {
        det->params = *params;
    } else {
        det->params = AETHER_DEFAULT_PARAMS;
    }
    det->current_state = AETHER_BOTTLENECK_UPLINK;
}

enum aether_bottleneck_state aether_bottleneck_detector_update(
    struct aether_bottleneck_detector* det,
    double delta_d_k, double dwell_time,
    double rtt_e2e, double ave_rtt) {
    
    double ratio = (ave_rtt > 0) ? (dwell_time / ave_rtt) : 0;
    
    if (delta_d_k < 0) {
        det->consecutive_negative_count++;
        det->consecutive_positive_count = 0;
    } else if (delta_d_k > 0) {
        det->consecutive_positive_count++;
        det->consecutive_negative_count = 0;
    } else {
        det->consecutive_negative_count = 0;
        det->consecutive_positive_count = 0;
    }
    
    enum aether_bottleneck_state prev_state = det->current_state;
    
    if (det->current_state == AETHER_BOTTLENECK_UPLINK) {
        if (det->consecutive_negative_count >= det->params.consecutive_events_threshold &&
            ratio < det->params.dwell_rtt_ratio_threshold) {
            det->current_state = AETHER_BOTTLENECK_INTERNET;
            det->consecutive_negative_count = 0;
            det->transition_count++;
        }
    } else {
        if (det->consecutive_positive_count >= det->params.consecutive_events_threshold &&
            ratio > det->params.dwell_rtt_ratio_threshold) {
            det->current_state = AETHER_BOTTLENECK_UPLINK;
            det->consecutive_positive_count = 0;
            det->transition_count++;
        }
    }
    
    if (det->current_state != prev_state && det->state_change_callback) {
        det->state_change_callback(prev_state, det->current_state, det->callback_ctx);
    }
    
    return det->current_state;
}

void aether_bottleneck_detector_force_state(struct aether_bottleneck_detector* det, enum aether_bottleneck_state state) {
    enum aether_bottleneck_state prev = det->current_state;
    det->current_state = state;
    det->consecutive_negative_count = 0;
    det->consecutive_positive_count = 0;
    
    if (prev != state && det->state_change_callback) {
        det->state_change_callback(prev, state, det->callback_ctx);
    }
}

void aether_bottleneck_detector_reset(struct aether_bottleneck_detector* det) {
    if (!det) return;
    struct aether_params params = det->params;
    void (*cb)(enum aether_bottleneck_state, enum aether_bottleneck_state, void*) = det->state_change_callback;
    void* ctx = det->callback_ctx;
    
    aether_bottleneck_detector_init(det, &params);
    det->state_change_callback = cb;
    det->callback_ctx = ctx;
}

const char* aether_bottleneck_state_to_string(enum aether_bottleneck_state state) {
    switch (state) {
        case AETHER_BOTTLENECK_UPLINK: return "UPLINK_BOTTLENECK";
        case AETHER_BOTTLENECK_INTERNET: return "INTERNET_BOTTLENECK";
        default: return "UNKNOWN";
    }
}
