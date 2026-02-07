#include "aether_bottleneck_detector.hh"
#include <cmath>

namespace aether {

AetherBottleneckDetector::AetherBottleneckDetector(const AetherParams& params)
    : params_(params), 
      current_state_(BottleneckState::UPLINK_BOTTLENECK),
      consecutive_negative_count_(0),
      consecutive_positive_count_(0),
      transition_count_(0) {}

BottleneckState AetherBottleneckDetector::update(double delta_d_k, double dwell_time,
                        double rtt_e2e, double ave_rtt) {
    double rtt_non_wireless = rtt_e2e - 2.0 * dwell_time;
    if (rtt_non_wireless < 0) rtt_non_wireless = 0;
    
    double ratio = (ave_rtt > 0) ? (dwell_time / ave_rtt) : 0;
    
    if (delta_d_k < 0) {
        consecutive_negative_count_++;
        consecutive_positive_count_ = 0;
    } else if (delta_d_k > 0) {
        consecutive_positive_count_++;
        consecutive_negative_count_ = 0;
    } else {
        consecutive_negative_count_ = 0;
        consecutive_positive_count_ = 0;
    }
    
    BottleneckState prev_state = current_state_;
    
    if (current_state_ == BottleneckState::UPLINK_BOTTLENECK) {
        if (consecutive_negative_count_ >= params_.consecutive_events_threshold &&
            ratio < params_.dwell_rtt_ratio_threshold) {
            current_state_ = BottleneckState::INTERNET_BOTTLENECK;
            consecutive_negative_count_ = 0;
            transition_count_++;
        }
    } else {
        if (consecutive_positive_count_ >= params_.consecutive_events_threshold &&
            ratio > params_.dwell_rtt_ratio_threshold) {
            current_state_ = BottleneckState::UPLINK_BOTTLENECK;
            consecutive_positive_count_ = 0;
            transition_count_++;
        }
    }
    
    if (current_state_ != prev_state && state_change_callback_) {
        state_change_callback_(prev_state, current_state_);
    }
    
    return current_state_;
}

bool AetherBottleneckDetector::detect_uplink_bottleneck_from_rtt(
    double delta_d_k, double prev_delta_d,
    double rtt_non_wireless, double prev_rtt_non_wireless) const {
    
    bool dwell_increasing = delta_d_k > prev_delta_d;
    bool rtt_stable = std::abs(rtt_non_wireless - prev_rtt_non_wireless) < std::abs(delta_d_k);
    return dwell_increasing && rtt_stable;
}

void AetherBottleneckDetector::force_state(BottleneckState state) {
    BottleneckState prev = current_state_;
    current_state_ = state;
    consecutive_negative_count_ = 0;
    consecutive_positive_count_ = 0;
    
    if (prev != state && state_change_callback_) {
        state_change_callback_(prev, state);
    }
}

void AetherBottleneckDetector::set_state_change_callback(StateChangeCallback callback) {
    state_change_callback_ = std::move(callback);
}

void AetherBottleneckDetector::reset() {
    current_state_ = BottleneckState::UPLINK_BOTTLENECK;
    consecutive_negative_count_ = 0;
    consecutive_positive_count_ = 0;
    transition_count_ = 0;
}

const char* AetherBottleneckDetector::state_to_string(BottleneckState state) {
    switch (state) {
        case BottleneckState::UPLINK_BOTTLENECK: return "UPLINK_BOTTLENECK";
        case BottleneckState::INTERNET_BOTTLENECK: return "INTERNET_BOTTLENECK";
        default: return "UNKNOWN";
    }
}

} // namespace aether
