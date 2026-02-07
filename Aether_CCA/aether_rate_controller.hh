#ifndef AETHER_RATE_CONTROLLER_HH
#define AETHER_RATE_CONTROLLER_HH

#include "aether_types.hh"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace aether {

class AetherRateController {
public:
    explicit AetherRateController(const AetherParams& params = AetherParams());

    double estimate_capacity(const std::vector<DwellSample>& samples);
    double multiplicative_factor(double rate, double capacity) const;
    double latency_factor(double rate, double capacity, double dwell_time) const;
    double update_cwnd(double current_cwnd, double current_rate, double dwell_time, double rtt);
    double calculate_pacing_rate(double cwnd, double rtt) const;

    void handle_sudden_degradation(double current_cwnd, double current_rate, size_t queue_size, uint64_t timestamp);
    double complete_drain(uint64_t drain_end_time);

    size_t get_probe_packet_count() const;
    size_t update_probe_count(bool queue_buildup_observed);

    double estimated_capacity() const { return estimated_capacity_; }
    CongestionState congestion_state() const { return congestion_state_; }
    bool is_frozen() const { return congestion_state_ == CongestionState::DRAINING; }
    void reset();
    void complete_recovery();

private:
    AetherParams params_;
    double estimated_capacity_;
    CongestionState congestion_state_;
    size_t probe_count_;
    double frozen_cwnd_;
    double frozen_pacing_rate_;
    uint64_t drain_start_time_;
    uint64_t draining_end_time_;
    size_t drain_queue_size_;
};

} // namespace aether

#endif // AETHER_RATE_CONTROLLER_HH
