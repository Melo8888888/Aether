#include "aether_monitor.h"
#include <string.h>
#include <math.h>

void aether_monitor_init(struct aether_monitor* mon, const struct aether_params* params) {
    if (!mon) return;
    memset(mon, 0, sizeof(struct aether_monitor));
    if (params) {
        mon->params = *params;
    } else {
        mon->params = AETHER_DEFAULT_PARAMS;
    }
}

u64 aether_monitor_on_packet_enqueue(struct aether_monitor* mon, size_t packet_size, u64 timestamp) {
    u64 seq = mon->next_seq_num++;
    size_t idx = seq % AETHER_MAX_PENDING_PACKETS;
    
    struct aether_packet_meta* meta = &mon->pending_packets[idx];
    meta->seq_num = seq;
    meta->enqueue_time_ms = timestamp;
    meta->packet_size = packet_size;
    meta->departed = false;
    meta->dequeue_time_ms = 0;
    
    mon->current_queue_length++;
    return seq;
}

bool aether_monitor_on_packet_dequeue(struct aether_monitor* mon, u64 seq_num, u64 timestamp, struct aether_dwell_sample* out_sample) {
    size_t idx = seq_num % AETHER_MAX_PENDING_PACKETS;
    struct aether_packet_meta* meta = &mon->pending_packets[idx];
    
    if (meta->seq_num != seq_num) {
        return false;
    }
    
    meta->dequeue_time_ms = timestamp;
    meta->departed = true;
    
    double dwell_time = 0;
    if (meta->dequeue_time_ms > meta->enqueue_time_ms) {
        dwell_time = (double)(meta->dequeue_time_ms - meta->enqueue_time_ms);
    }
    
    struct aether_dwell_sample sample;
    sample.timestamp_ms = timestamp;
    sample.dwell_time_ms = dwell_time;
    sample.queue_length = mon->current_queue_length;
    sample.packet_size_bytes = meta->packet_size;
    
    mon->dwell_history[mon->history_head] = sample;
    mon->history_head = (mon->history_head + 1) % AETHER_HISTORY_SIZE;
    if (mon->history_count < AETHER_HISTORY_SIZE) {
        mon->history_count++;
    }
    
    mon->stats.packets_monitored++;
    if (mon->stats.packets_monitored == 1) {
        mon->stats.avg_dwell_time_ms = dwell_time;
    } else {
        mon->stats.avg_dwell_time_ms = (mon->stats.avg_dwell_time_ms * (mon->stats.packets_monitored - 1) + dwell_time) / mon->stats.packets_monitored;
    }
    mon->stats.max_dwell_time_ms = fmax(mon->stats.max_dwell_time_ms, dwell_time);
    
    double delta = dwell_time - mon->prev_dwell_time_ms;
    mon->prev_dwell_time_ms = dwell_time;
    mon->last_delta_d = delta;
    
    if (mon->current_queue_length > 0) {
        mon->current_queue_length--;
    }
    
    if (out_sample) {
        *out_sample = sample;
    }
    
    if (mon->dwell_callback) {
        mon->dwell_callback(&sample, mon->callback_ctx);
    }
    
    return true;
}

double aether_monitor_get_last_delta_dwell(const struct aether_monitor* mon) {
    return mon->last_delta_d;
}

double aether_monitor_get_last_dwell_time(const struct aether_monitor* mon) {
    return mon->prev_dwell_time_ms;
}

size_t aether_monitor_get_queue_length(const struct aether_monitor* mon) {
    return mon->current_queue_length;
}

bool aether_monitor_is_queue_empty(const struct aether_monitor* mon) {
    return mon->current_queue_length == 0;
}

bool aether_monitor_detect_sudden_degradation(const struct aether_monitor* mon, double prev_delta) {
    if (prev_delta <= 0) return false;
    return mon->last_delta_d >= mon->params.c_threshold * prev_delta;
}

void aether_monitor_reset(struct aether_monitor* mon) {
    if (!mon) return;
    struct aether_params params = mon->params;
    aether_monitor_init(mon, &params);
}

size_t aether_monitor_get_recent_samples(const struct aether_monitor* mon, size_t count, struct aether_dwell_sample* out_buf, size_t buf_size) {
    if (!mon || !out_buf || buf_size == 0) return 0;
    
    if (count == 0) count = mon->params.gamma;
    if (count > mon->history_count) count = mon->history_count;
    if (count > buf_size) count = buf_size;
    
    size_t extracted = 0;
    size_t current_idx = (mon->history_head == 0) ? (AETHER_HISTORY_SIZE - 1) : (mon->history_head - 1);
    
    for (size_t i = 0; i < count; ++i) {
        out_buf[i] = mon->dwell_history[current_idx];
        if (current_idx == 0) current_idx = AETHER_HISTORY_SIZE - 1;
        else current_idx--;
        extracted++;
    }
    
    return extracted;
}
