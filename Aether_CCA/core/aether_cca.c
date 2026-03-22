#include "aether_cca.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void on_bottleneck_change(enum aether_bottleneck_state from, enum aether_bottleneck_state to, void* ctx) {
    struct aether_cca* cca = (struct aether_cca*)ctx;
    cca->monitor.stats.bottleneck_transitions++;
    cca->use_aether_control = (to == AETHER_BOTTLENECK_UPLINK);
    if (to == AETHER_BOTTLENECK_INTERNET) {
        aether_rate_controller_complete_recovery(&cca->controller);
    }
}

void aether_cca_init(struct aether_cca* cca, const struct aether_params* params) {
    if (!cca) return;
    memset(cca, 0, sizeof(struct aether_cca));
    
    if (params) {
        cca->params = *params;
    } else {
        cca->params = AETHER_DEFAULT_PARAMS;
    }
    
    aether_monitor_init(&cca->monitor, &cca->params);
    aether_bottleneck_detector_init(&cca->detector, &cca->params);
    aether_rate_controller_init(&cca->controller, &cca->params);
    
    cca->tcp_state.cwnd = 10 * 1460;
    cca->tcp_state.min_rtt_ms = 1e9;
    cca->use_aether_control = true;
    
    cca->detector.state_change_callback = on_bottleneck_change;
    cca->detector.callback_ctx = cca;
}

u64 aether_cca_on_packet_enqueue(struct aether_cca* cca, size_t packet_size, u64 timestamp) {
    return aether_monitor_on_packet_enqueue(&cca->monitor, packet_size, timestamp);
}

void aether_cca_on_packet_dequeue(struct aether_cca* cca, u64 seq_num, u64 timestamp) {
    struct aether_dwell_sample sample;
    if (!aether_monitor_on_packet_dequeue(&cca->monitor, seq_num, timestamp, &sample)) {
        return;
    }
    
    double delta_d = aether_monitor_get_last_delta_dwell(&cca->monitor);
    double dwell_time = aether_monitor_get_last_dwell_time(&cca->monitor);
    
    if (aether_monitor_detect_sudden_degradation(&cca->monitor, cca->prev_delta_d)) {
        size_t queue_bytes = aether_monitor_get_queue_length(&cca->monitor) * 1460;
        aether_rate_controller_handle_sudden_degradation(&cca->controller, cca->tcp_state.cwnd, 
                                                       cca->tcp_state.pacing_rate, queue_bytes, timestamp);
        cca->monitor.stats.sudden_degradation_events++;
    }
    
    aether_bottleneck_detector_update(&cca->detector, delta_d, dwell_time, cca->tcp_state.rtt_ms, cca->tcp_state.ave_rtt_ms);
    
    if (cca->detector.current_state == AETHER_BOTTLENECK_UPLINK) {
        struct aether_dwell_sample samples_buf[20];
        size_t count = aether_monitor_get_recent_samples(&cca->monitor, cca->params.gamma, samples_buf, 20);
        aether_rate_controller_estimate_capacity(&cca->controller, samples_buf, count);
    }
    
    cca->prev_delta_d = delta_d;
    
    if (cca->controller.congestion_state == AETHER_STATE_DRAINING && aether_monitor_is_queue_empty(&cca->monitor)) {
        double new_rate = aether_rate_controller_complete_drain(&cca->controller, timestamp);
        if (new_rate > 0) cca->tcp_state.pacing_rate = new_rate;
    }
}

void aether_cca_on_ack_received(struct aether_cca* cca, size_t bytes_acked, double rtt, u64 timestamp) {
    cca->tcp_state.bytes_acked += bytes_acked;
    cca->tcp_state.rtt_ms = rtt;
    if (rtt < cca->tcp_state.min_rtt_ms) cca->tcp_state.min_rtt_ms = rtt;
    
    if (cca->tcp_state.bytes_acked == bytes_acked) {
        cca->tcp_state.ave_rtt_ms = rtt;
    } else {
        cca->tcp_state.ave_rtt_ms = 0.875 * cca->tcp_state.ave_rtt_ms + 0.125 * rtt;
    }
    
    cca->use_aether_control = (cca->detector.current_state == AETHER_BOTTLENECK_UPLINK);
    if (!cca->use_aether_control) return;
    
    double dwell = aether_monitor_get_last_dwell_time(&cca->monitor);
    double rtt_non_wireless = rtt - 2 * dwell;
    
    if (rtt_non_wireless > cca->prev_rtt_non_wireless + fabs(aether_monitor_get_last_delta_dwell(&cca->monitor))) {
        cca->use_aether_control = false;
        aether_bottleneck_detector_force_state(&cca->detector, AETHER_BOTTLENECK_INTERNET);
        cca->prev_rtt_non_wireless = rtt_non_wireless;
        return;
    }
    cca->prev_rtt_non_wireless = rtt_non_wireless;
    
    double new_cwnd = aether_rate_controller_update_cwnd(&cca->controller, cca->tcp_state.cwnd, cca->tcp_state.pacing_rate, dwell, rtt);
    cca->tcp_state.cwnd = new_cwnd;
    cca->tcp_state.pacing_rate = aether_rate_controller_calculate_pacing_rate(&cca->controller, new_cwnd, cca->tcp_state.ave_rtt_ms);
}

size_t aether_cca_on_queue_empty(struct aether_cca* cca) {
    if (cca->detector.current_state == AETHER_BOTTLENECK_UPLINK) {
        cca->monitor.stats.capacity_probes++;
        return aether_rate_controller_get_probe_packet_count(&cca->controller);
    }
    return 0;
}

struct aether_decision aether_cca_get_decision(const struct aether_cca* cca) {
    struct aether_decision decision;
    decision.new_cwnd = cca->tcp_state.cwnd;
    decision.new_pacing_rate = cca->tcp_state.pacing_rate;
    decision.bottleneck = cca->detector.current_state;
    decision.congestion = cca->controller.congestion_state;
    decision.estimated_capacity = cca->controller.estimated_capacity;
    decision.use_default_cca = !cca->use_aether_control;
    return decision;
}

void aether_cca_reset(struct aether_cca* cca) {
    if (!cca) return;
    struct aether_params params = cca->params;
    aether_cca_init(cca, &params);
}

void aether_cca_to_string(const struct aether_cca* cca, char* buffer, size_t size) {
    if (!buffer || size == 0) return;
    snprintf(buffer, size, "AetherCCA[cwnd=%.2f, rate=%.2f, cap=%.2f, bn=%s, active=%s]",
             cca->tcp_state.cwnd,
             cca->tcp_state.pacing_rate,
             cca->controller.estimated_capacity,
             aether_bottleneck_state_to_string(cca->detector.current_state),
             cca->use_aether_control ? "yes" : "no");
}
