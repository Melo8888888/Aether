#ifndef AETHER_MONITOR_H
#define AETHER_MONITOR_H

#include "aether_types.h"

#define AETHER_HISTORY_SIZE 100
#define AETHER_MAX_PENDING_PACKETS 2048

struct aether_monitor {
    struct aether_params params;
    struct aether_packet_meta pending_packets[AETHER_MAX_PENDING_PACKETS];
    struct aether_dwell_sample dwell_history[AETHER_HISTORY_SIZE];
    size_t history_head;
    size_t history_count;
    
    u64 next_seq_num;
    double prev_dwell_time_ms;
    double last_delta_d;
    size_t current_queue_length;
    
    struct aether_stats stats;
    
    void (*dwell_callback)(const struct aether_dwell_sample* sample, void* ctx);
    void* callback_ctx;
};

void aether_monitor_init(struct aether_monitor* mon, const struct aether_params* params);
u64 aether_monitor_on_packet_enqueue(struct aether_monitor* mon, size_t packet_size, u64 timestamp);
bool aether_monitor_on_packet_dequeue(struct aether_monitor* mon, u64 seq_num, u64 timestamp, struct aether_dwell_sample* out_sample);

double aether_monitor_get_last_delta_dwell(const struct aether_monitor* mon);
double aether_monitor_get_last_dwell_time(const struct aether_monitor* mon);
size_t aether_monitor_get_queue_length(const struct aether_monitor* mon);
bool aether_monitor_is_queue_empty(const struct aether_monitor* mon);
bool aether_monitor_detect_sudden_degradation(const struct aether_monitor* mon, double prev_delta);
void aether_monitor_reset(struct aether_monitor* mon);

size_t aether_monitor_get_recent_samples(const struct aether_monitor* mon, size_t count, struct aether_dwell_sample* out_buf, size_t buf_size);

#endif
