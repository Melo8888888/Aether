#include "aether_rate_controller.hh"

namespace aether {

AetherRateController::AetherRateController(const AetherParams& params)
    : params_(params), 
      estimated_capacity_(0),
      congestion_state_(CongestionState::NORMAL),
      probe_count_(0),
      frozen_cwnd_(0),
      frozen_pacing_rate_(0),
      drain_start_time_(0),
      drain_queue_size_(0) {}

double AetherRateController::estimate_capacity(const std::vector<DwellSample>& samples) {
    if (samples.empty()) return estimated_capacity_;
    
    double sum = 0;
    size_t count = 0;
    
    for (const auto& sample : samples) {
        if (sample.dwell_time_ms > 0 && sample.queue_length > 0) {
            double queue_bytes = sample.queue_length * sample.packet_size_bytes;
            double dwell_sec = sample.dwell_time_ms / 1000.0;
            sum += queue_bytes / dwell_sec;
            count++;
        }
    }
    
    if (count > 0) estimated_capacity_ = sum / count;
    return estimated_capacity_;
}

double AetherRateController::multiplicative_factor(double rate, double capacity) const {
    if (rate <= 0 || capacity <= 0) return 1.0;
    double f = 1.0 + std::log(capacity / rate);
    return std::clamp(f, 0.5, 2.0);
}

double AetherRateController::latency_factor(double rate, double capacity, double dwell_time) const {
    if (rate <= capacity) return 0;
    double excess = rate - capacity;
    double dwell_sec = dwell_time / 1000.0;
    return params_.alpha * dwell_sec * excess;
}

double AetherRateController::update_cwnd(double current_cwnd, double current_rate, double dwell_time, double rtt) {
    if (congestion_state_ == CongestionState::DRAINING) return current_cwnd;
    if (estimated_capacity_ <= 0) return current_cwnd;
    
    double f = multiplicative_factor(current_rate, estimated_capacity_);
    double v = latency_factor(current_rate, estimated_capacity_, dwell_time);
    
    double new_cwnd = current_cwnd * f - v;
    return std::max(new_cwnd, 2.0 * 1460.0);
}

double AetherRateController::calculate_pacing_rate(double cwnd, double rtt) const {
    return (rtt <= 0) ? 0 : cwnd / (rtt / 1000.0);
}

void AetherRateController::handle_sudden_degradation(double current_cwnd, double current_rate, size_t queue_size, uint64_t timestamp) {
    if (congestion_state_ == CongestionState::DRAINING) return;
    
    frozen_cwnd_ = current_cwnd;
    frozen_pacing_rate_ = current_rate;
    drain_start_time_ = timestamp;
    drain_queue_size_ = queue_size;
    congestion_state_ = CongestionState::DRAINING;
}

double AetherRateController::complete_drain(uint64_t drain_end_time) {
    if (congestion_state_ != CongestionState::DRAINING) return 0;
    
    double drain_time_ms = draining_end_time_ = drain_end_time - drain_start_time_;
    if (drain_time_ms <= 0) {
        congestion_state_ = CongestionState::NORMAL;
        return frozen_pacing_rate_;
    }
    
    double drain_rate = drain_queue_size_ / (drain_time_ms / 1000.0);
    double new_rate;
    
    if (drain_rate >= estimated_capacity_) {
        new_rate = frozen_pacing_rate_;
    } else {
        new_rate = drain_rate;
        estimated_capacity_ = drain_rate;
    }
    
    congestion_state_ = CongestionState::RECOVERING;
    return new_rate;
}

size_t AetherRateController::get_probe_packet_count() const {
    return params_.K;
}

size_t AetherRateController::update_probe_count(bool queue_buildup_observed) {
    if (!queue_buildup_observed) {
        probe_count_ = std::min(probe_count_ * 2, static_cast<size_t>(128));
        return probe_count_;
    }
    probe_count_ = params_.K;
    return probe_count_;
}

void AetherRateController::reset() {
    estimated_capacity_ = 0;
    congestion_state_ = CongestionState::NORMAL;
    probe_count_ = params_.K;
    frozen_cwnd_ = 0;
    frozen_pacing_rate_ = 0;
}

void AetherRateController::complete_recovery() {
    if (congestion_state_ == CongestionState::RECOVERING) {
        congestion_state_ = CongestionState::NORMAL;
    }
}

} // namespace aether
