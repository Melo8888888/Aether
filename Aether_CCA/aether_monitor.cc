#include "aether_monitor.hh"

namespace aether {

AetherMonitor::AetherMonitor(const AetherParams& params)
    : params_(params), next_seq_num_(0), 
      prev_dwell_time_ms_(0), last_delta_d_(0),
      current_queue_length_(0) {}

uint64_t AetherMonitor::on_packet_enqueue(size_t packet_size, uint64_t timestamp) {
    uint64_t seq = next_seq_num_++;
    PacketMeta meta(seq, timestamp, packet_size);
    pending_packets_[seq] = meta;
    current_queue_length_++;
    return seq;
}

std::optional<DwellSample> AetherMonitor::on_packet_dequeue(uint64_t seq_num, uint64_t timestamp) {
    auto it = pending_packets_.find(seq_num);
    if (it == pending_packets_.end()) return std::nullopt;

    PacketMeta& meta = it->second;
    meta.dequeue_time_ms = timestamp;
    meta.departed = true;
    
    double dwell_time = meta.dwell_time_ms();
    DwellSample sample(timestamp, dwell_time, current_queue_length_, meta.packet_size);
    
    dwell_history_.push_back(sample);
    if (dwell_history_.size() > max_history_size_) {
        dwell_history_.pop_front();
    }
    
    stats_.packets_monitored++;
    if (stats_.packets_monitored == 1) {
        stats_.avg_dwell_time_ms = dwell_time;
    } else {
        stats_.avg_dwell_time_ms = (stats_.avg_dwell_time_ms * (stats_.packets_monitored - 1) + dwell_time) / stats_.packets_monitored;
    }
    stats_.max_dwell_time_ms = std::max(stats_.max_dwell_time_ms, dwell_time);
    
    double delta = dwell_time - prev_dwell_time_ms_;
    prev_dwell_time_ms_ = dwell_time;
    last_delta_d_ = delta;
    
    pending_packets_.erase(it);
    current_queue_length_--;
    
    if (dwell_callback_) dwell_callback_(sample);
    
    return sample;
}

std::vector<DwellSample> AetherMonitor::get_recent_samples(size_t count) const {
    if (count == 0) count = params_.gamma;
    std::vector<DwellSample> samples;
    size_t start = (dwell_history_.size() > count) ? (dwell_history_.size() - count) : 0;
    for (size_t i = start; i < dwell_history_.size(); ++i) {
        samples.push_back(dwell_history_[i]);
    }
    return samples;
}

bool AetherMonitor::detect_sudden_degradation(double prev_delta) const {
    if (prev_delta <= 0) return false;
    return last_delta_d_ >= params_.C_threshold * prev_delta;
}

void AetherMonitor::reset() {
    pending_packets_.clear();
    dwell_history_.clear();
    next_seq_num_ = 0;
    prev_dwell_time_ms_ = 0;
    last_delta_d_ = 0;
    current_queue_length_ = 0;
    stats_.reset();
}

} // namespace aether
