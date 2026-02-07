#ifndef AETHER_CCA_HH
#define AETHER_CCA_HH

#include "aether_types.hh"
#include "aether_monitor.hh"
#include "aether_bottleneck_detector.hh"
#include "aether_rate_controller.hh"
#include <sstream>
#include <string>

namespace aether {

class AetherCCA {
public:
    explicit AetherCCA(const AetherParams& params = AetherParams());

    uint64_t on_packet_enqueue(size_t packet_size, uint64_t timestamp);
    void on_packet_dequeue(uint64_t seq_num, uint64_t timestamp);
    void on_ack_received(size_t bytes_acked, double rtt, uint64_t timestamp);
    size_t on_queue_empty();

    AetherDecision get_decision() const;
    
    double get_cwnd() const { return tcp_state_.cwnd; }
    double get_pacing_rate() const { return tcp_state_.pacing_rate; }
    bool is_aether_active() const { return use_aether_control_; }
    BottleneckState bottleneck_state() const { return detector_.current_state(); }
    double estimated_capacity() const { return controller_.estimated_capacity(); }
    size_t queue_length() const { return monitor_.current_queue_length(); }
    const AetherStats& statistics() const { return stats_; }
    const TCPState& tcp_state() const { return tcp_state_; }

    void set_tcp_state(const TCPState& state) { tcp_state_ = state; }
    void reset();
    std::string to_string() const;

private:
    void on_bottleneck_change(BottleneckState from, BottleneckState to);

    AetherParams params_;
    AetherMonitor monitor_;
    AetherBottleneckDetector detector_;
    AetherRateController controller_;
    TCPState tcp_state_;
    
    double prev_delta_d_;
    double prev_rtt_non_wireless_;
    bool use_aether_control_;
    AetherStats stats_;
};

} // namespace aether

#endif // AETHER_CCA_HH
