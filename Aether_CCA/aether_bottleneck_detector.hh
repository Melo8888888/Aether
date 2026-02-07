#ifndef AETHER_BOTTLENECK_DETECTOR_HH
#define AETHER_BOTTLENECK_DETECTOR_HH

#include "aether_types.hh"
#include <functional>

namespace aether {

class AetherBottleneckDetector {
public:
    using StateChangeCallback = std::function<void(BottleneckState, BottleneckState)>;

    explicit AetherBottleneckDetector(const AetherParams& params = AetherParams());

    BottleneckState update(double delta_d_k, double dwell_time,
                            double rtt_e2e, double ave_rtt);

    bool detect_uplink_bottleneck_from_rtt(
        double delta_d_k, double prev_delta_d,
        double rtt_non_wireless, double prev_rtt_non_wireless) const;

    void force_state(BottleneckState state);
    
    BottleneckState current_state() const { return current_state_; }
    bool is_uplink_bottleneck() const { return current_state_ == BottleneckState::UPLINK_BOTTLENECK; }
    uint64_t transition_count() const { return transition_count_; }

    void set_state_change_callback(StateChangeCallback callback);
    void reset();
    
    static const char* state_to_string(BottleneckState state);

private:
    AetherParams params_;
    BottleneckState current_state_;
    size_t consecutive_negative_count_;
    size_t consecutive_positive_count_;
    uint64_t transition_count_;
    StateChangeCallback state_change_callback_;
};

} // namespace aether

#endif // AETHER_BOTTLENECK_DETECTOR_HH
