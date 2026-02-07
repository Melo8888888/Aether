#ifndef AETHER_CCA_H
#define AETHER_CCA_H

#include "aether_types.h"
#include "aether_monitor.h"
#include "aether_bottleneck_detector.h"
#include "aether_rate_controller.h"

struct aether_cca {
    struct aether_params params;
    struct aether_monitor monitor;
    struct aether_bottleneck_detector detector;
    struct aether_rate_controller controller;
    struct aether_tcp_state tcp_state;
    
    double prev_delta_d;
    double prev_rtt_non_wireless;
    bool use_aether_control;
};

void aether_cca_init(struct aether_cca* cca, const struct aether_params* params);

u64 aether_cca_on_packet_enqueue(struct aether_cca* cca, size_t packet_size, u64 timestamp);
void aether_cca_on_packet_dequeue(struct aether_cca* cca, u64 seq_num, u64 timestamp);
void aether_cca_on_ack_received(struct aether_cca* cca, size_t bytes_acked, double rtt, u64 timestamp);
size_t aether_cca_on_queue_empty(struct aether_cca* cca);

struct aether_decision aether_cca_get_decision(const struct aether_cca* cca);
void aether_cca_reset(struct aether_cca* cca);

void aether_cca_to_string(const struct aether_cca* cca, char* buffer, size_t size);

#endif
