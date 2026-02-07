#include "aether_cca.hh"

namespace aether {

AetherCCA::AetherCCA(const AetherParams& params)
    : params_(params), monitor_(params), detector_(params), controller_(params),
      tcp_state_(), prev_delta_d_(0), prev_rtt_non_wireless_(0), use_aether_control_(true) {
    
    detector_.set_state_change_callback([this](BottleneckState from, BottleneckState to) {
        on_bottleneck_change(from, to);
    });
}

uint64_t AetherCCA::on_packet_enqueue(size_t packet_size, uint64_t timestamp) {
    return monitor_.on_packet_enqueue(packet_size, timestamp);
}

void AetherCCA::on_packet_dequeue(uint64_t seq_num, uint64_t timestamp) {
    auto sample = monitor_.on_packet_dequeue(seq_num, timestamp);
    if (!sample) return;
    
    double delta_d = monitor_.last_delta_dwell();
    double dwell_time = monitor_.last_dwell_time();
    
    if (monitor_.detect_sudden_degradation(prev_delta_d_)) {
        size_t queue_bytes = monitor_.current_queue_length() * 1460;
        controller_.handle_sudden_degradation(tcp_state_.cwnd, tcp_state_.pacing_rate, queue_bytes, timestamp);
        stats_.sudden_degradation_events++;
    }
    
    detector_.update(delta_d, dwell_time, tcp_state_.rtt_ms, tcp_state_.ave_rtt_ms);
    
    if (detector_.is_uplink_bottleneck()) {
        auto samples = monitor_.get_recent_samples(params_.gamma);
        controller_.estimate_capacity(samples);
    }
    
    prev_delta_d_ = delta_d;
    
    if (controller_.is_frozen() && monitor_.is_queue_empty()) {
        double new_rate = controller_.complete_drain(timestamp);
        if (new_rate > 0) tcp_state_.pacing_rate = new_rate;
    }
}

void AetherCCA::on_ack_received(size_t bytes_acked, double rtt, uint64_t timestamp) {
    tcp_state_.bytes_acked += bytes_acked;
    tcp_state_.rtt_ms = rtt;
    tcp_state_.min_rtt_ms = std::min(tcp_state_.min_rtt_ms, rtt);
    tcp_state_.ave_rtt_ms = 0.875 * tcp_state_.ave_rtt_ms + 0.125 * rtt;
    
    use_aether_control_ = detector_.is_uplink_bottleneck();
    if (!use_aether_control_) return;
    
    double dwell = monitor_.last_dwell_time();
    double rtt_non_wireless = rtt - 2 * dwell;
    if (rtt_non_wireless > prev_rtt_non_wireless_ + std::abs(monitor_.last_delta_dwell())) {
        use_aether_control_ = false;
        detector_.force_state(BottleneckState::INTERNET_BOTTLENECK);
        prev_rtt_non_wireless_ = rtt_non_wireless;
        return;
    }
    prev_rtt_non_wireless_ = rtt_non_wireless;
    
    double new_cwnd = controller_.update_cwnd(tcp_state_.cwnd, tcp_state_.pacing_rate, dwell, rtt);
    tcp_state_.cwnd = new_cwnd;
    tcp_state_.pacing_rate = controller_.calculate_pacing_rate(new_cwnd, tcp_state_.ave_rtt_ms);
}

size_t AetherCCA::on_queue_empty() {
    if (detector_.is_uplink_bottleneck()) {
        stats_.capacity_probes++;
        return controller_.get_probe_packet_count();
    }
    return 0;
}

AetherDecision AetherCCA::get_decision() const {
    AetherDecision decision;
    decision.new_cwnd = tcp_state_.cwnd;
    decision.new_pacing_rate = tcp_state_.pacing_rate;
    decision.bottleneck = detector_.current_state();
    decision.congestion = controller_.congestion_state();
    decision.estimated_capacity = controller_.estimated_capacity();
    decision.use_default_cca = !use_aether_control_;
    return decision;
}

void AetherCCA::reset() {
    monitor_.reset();
    detector_.reset();
    controller_.reset();
    tcp_state_ = TCPState();
    prev_delta_d_ = 0;
    prev_rtt_non_wireless_ = 0;
    use_aether_control_ = true;
    stats_.reset();
}

std::string AetherCCA::to_string() const {
    std::ostringstream oss;
    oss << "AetherCCA[cwnd=" << tcp_state_.cwnd
        << ", rate=" << tcp_state_.pacing_rate
        << ", capacity=" << controller_.estimated_capacity()
        << ", bottleneck=" << AetherBottleneckDetector::state_to_string(detector_.current_state())
        << ", active=" << (use_aether_control_ ? "yes" : "no") << "]";
    return oss.str();
}

void AetherCCA::on_bottleneck_change(BottleneckState from, BottleneckState to) {
    stats_.bottleneck_transitions++;
    use_aether_control_ = (to == BottleneckState::UPLINK_BOTTLENECK);
    if (to == BottleneckState::INTERNET_BOTTLENECK) {
        controller_.complete_recovery();
    }
}

} // namespace aether
