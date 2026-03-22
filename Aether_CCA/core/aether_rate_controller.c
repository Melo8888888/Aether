#include "aether_rate_controller.h"
#include <string.h>
#include <math.h>

static inline double fclamp(double d, double min, double max) {
  const double t = (d < min) ? min : d;
  return (t > max) ? max : t;
}

void aether_rate_controller_init(struct aether_rate_controller* rc, const struct aether_params* params) {
    if (!rc) return;
    memset(rc, 0, sizeof(struct aether_rate_controller));
    if (params) {
        rc->params = *params;
    } else {
        rc->params = AETHER_DEFAULT_PARAMS;
    }
    rc->congestion_state = AETHER_STATE_NORMAL;
    rc->probe_count = rc->params.k;
}

double aether_rate_controller_estimate_capacity(struct aether_rate_controller* rc, const struct aether_dwell_sample* samples, size_t count) {
    if (count == 0 || !samples) return rc->estimated_capacity;
    
    double sum = 0;
    size_t valid_count = 0;
    
    for (size_t i = 0; i < count; i++) {
        if (samples[i].dwell_time_ms > 0 && samples[i].queue_length > 0) {
            double queue_bytes = samples[i].queue_length * samples[i].packet_size_bytes;
            double dwell_sec = samples[i].dwell_time_ms / 1000.0;
            sum += queue_bytes / dwell_sec;
            valid_count++;
        }
    }
    
    if (valid_count > 0) rc->estimated_capacity = sum / valid_count;
    return rc->estimated_capacity;
}

static double multiplicative_factor(double rate, double capacity) {
    if (rate <= 0 || capacity <= 0) return 1.0;
    double f = 1.0 + log(capacity / rate);
    return fclamp(f, 0.5, 2.0);
}

static double latency_factor(struct aether_rate_controller* rc, double rate, double capacity, double dwell_time) {
    if (rate <= capacity) return 0;
    double excess = rate - capacity;
    double dwell_sec = dwell_time / 1000.0;
    return rc->params.alpha * dwell_sec * excess;
}

double aether_rate_controller_update_cwnd(struct aether_rate_controller* rc, double current_cwnd, double current_rate, double dwell_time, double rtt) {
    if (rc->congestion_state == AETHER_STATE_DRAINING) return current_cwnd;
    if (rc->estimated_capacity <= 0) return current_cwnd;
    
    double f = multiplicative_factor(current_rate, rc->estimated_capacity);
    double v = latency_factor(rc, current_rate, rc->estimated_capacity, dwell_time);
    
    double new_cwnd = current_cwnd * f - v;
    return fmax(new_cwnd, 2.0 * 1460.0);
}

double aether_rate_controller_calculate_pacing_rate(const struct aether_rate_controller* rc, double cwnd, double rtt) {
    return (rtt <= 0) ? 0 : cwnd / (rtt / 1000.0);
}

void aether_rate_controller_handle_sudden_degradation(struct aether_rate_controller* rc, double current_cwnd, double current_rate, size_t queue_size, u64 timestamp) {
    if (rc->congestion_state == AETHER_STATE_DRAINING) return;
    
    rc->frozen_cwnd = current_cwnd;
    rc->frozen_pacing_rate = current_rate;
    rc->drain_start_time = timestamp;
    rc->drain_queue_size = queue_size;
    rc->congestion_state = AETHER_STATE_DRAINING;
}

double aether_rate_controller_complete_drain(struct aether_rate_controller* rc, u64 drain_end_time) {
    if (rc->congestion_state != AETHER_STATE_DRAINING) return 0;
    
    double drain_time_ms = drain_end_time - rc->drain_start_time;
    rc->draining_end_time = drain_time_ms;
    
    if (drain_time_ms <= 0) {
        rc->congestion_state = AETHER_STATE_NORMAL;
        return rc->frozen_pacing_rate;
    }
    
    double drain_rate = rc->drain_queue_size / (drain_time_ms / 1000.0);
    double new_rate;
    
    if (drain_rate >= rc->estimated_capacity) {
        new_rate = rc->frozen_pacing_rate;
    } else {
        new_rate = drain_rate;
        rc->estimated_capacity = drain_rate;
    }
    
    rc->congestion_state = AETHER_STATE_RECOVERING;
    return new_rate;
}

size_t aether_rate_controller_get_probe_packet_count(const struct aether_rate_controller* rc) {
    return rc->params.k;
}

size_t aether_rate_controller_update_probe_count(struct aether_rate_controller* rc, bool queue_buildup_observed) {
    if (!queue_buildup_observed) {
        size_t current = rc->probe_count;
        rc->probe_count = (current * 2 > 128) ? 128 : (current * 2);
        return rc->probe_count;
    }
    rc->probe_count = rc->params.k;
    return rc->probe_count;
}

void aether_rate_controller_complete_recovery(struct aether_rate_controller* rc) {
    if (rc->congestion_state == AETHER_STATE_RECOVERING) {
        rc->congestion_state = AETHER_STATE_NORMAL;
    }
}

void aether_rate_controller_reset(struct aether_rate_controller* rc) {
    if (!rc) return;
    struct aether_params params = rc->params;
    aether_rate_controller_init(rc, &params);
}
