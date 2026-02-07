#ifndef AETHER_MONITOR_HH
#define AETHER_MONITOR_HH

#include "aether_types.hh"
#include <deque>
#include <unordered_map>
#include <optional>
#include <functional>

namespace aether {

class AetherMonitor {
public:
    using DwellCallback = std::function<void(const DwellSample&)>;

    explicit AetherMonitor(const AetherParams& params = AetherParams());

    uint64_t on_packet_enqueue(size_t packet_size, uint64_t timestamp);
    std::optional<DwellSample> on_packet_dequeue(uint64_t seq_num, uint64_t timestamp);

    double last_delta_dwell() const { return last_delta_d_; }
    double last_dwell_time() const { return prev_dwell_time_ms_; }
    size_t current_queue_length() const { return current_queue_length_; }
    bool is_queue_empty() const { return current_queue_length_ == 0; }
    
    std::vector<DwellSample> get_recent_samples(size_t count = 0) const;
    bool detect_sudden_degradation(double prev_delta) const;

    void set_dwell_callback(DwellCallback callback) {
        dwell_callback_ = std::move(callback);
    }
    const AetherStats& statistics() const { return stats_; }
    void reset();

private:
    AetherParams params_;
    std::unordered_map<uint64_t, PacketMeta> pending_packets_;
    std::deque<DwellSample> dwell_history_;
    
    uint64_t next_seq_num_;
    double prev_dwell_time_ms_;
    double last_delta_d_;
    size_t current_queue_length_;
    
    static constexpr size_t max_history_size_ = 100;
    AetherStats stats_;
    DwellCallback dwell_callback_;
};

} // namespace aether

#endif // AETHER_MONITOR_HH
