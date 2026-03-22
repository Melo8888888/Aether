/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef TWO_TIER_QUEUE_MANAGER_HH
#define TWO_TIER_QUEUE_MANAGER_HH

#include <memory>
#include <string>
#include <functional>

#include "virtual_driver_queue.hh"
#include "virtual_modem_buffer.hh"
#include "signal_exporter.hh"
#include "queued_packet.hh"

namespace cellular_emulation {

struct TwoTierQueueConfig {
    VDQueueConfig driver_queue_config;
    VModemConfig modem_buffer_config;
    std::string signal_export_path = "/tmp/mm_virtual_driver_queue";
    bool enable_signal_export = true;
    bool enable_csv_logging = false;
    std::string csv_log_path = "/tmp/mm_vdqueue_log.csv";
    
    TwoTierQueueConfig() = default;
};

/*
 * Packet flow:
 *   Application -> VDQueue (driver layer) -> VModemBuf (modem) -> Link
 *
 * Backpressure flow:
 *   Link <- VModemBuf backpressure <- VDQueue buildup <- Application
 */
class TwoTierQueueManager {
public:
    explicit TwoTierQueueManager(const TwoTierQueueConfig& config);
    TwoTierQueueManager();
    ~TwoTierQueueManager();

    TwoTierQueueManager(const TwoTierQueueManager&) = delete;
    TwoTierQueueManager& operator=(const TwoTierQueueManager&) = delete;

    bool receive_packet(const std::string& packet, uint64_t arrival_time);
    size_t try_transfer_packets();
    QueuedPacket drain_packet();
    void update(uint64_t current_time);

    size_t driver_queue_size() const;
    size_t modem_buffer_size() const;
    size_t total_queued_packets() const;
    bool is_backpressure_active() const;
    uint64_t driver_queue_dwell_time(uint64_t current_time) const;

    const VDQueueStats& driver_queue_statistics() const;
    std::string statistics_summary() const;
    void reset_statistics();

    void export_signal_now(uint64_t current_time);
    void set_drop_callback(std::function<void(size_t, size_t)> callback);
    std::string to_string() const;

private:
    std::unique_ptr<VirtualDriverQueue> driver_queue_;
    std::unique_ptr<VirtualModemBuffer> modem_buffer_;
    std::shared_ptr<SignalExporter> signal_exporter_;

    TwoTierQueueConfig config_;

    uint64_t last_update_time_;
    uint64_t last_signal_export_time_;

    uint64_t total_packets_received_;
    uint64_t total_packets_transferred_;
    uint64_t total_backpressure_events_;

    std::function<void(size_t, size_t)> drop_callback_;

    void initialize();
    void export_signal(uint64_t current_time);
    void handle_backpressure_change(bool active);
};

} // namespace cellular_emulation

#endif /* TWO_TIER_QUEUE_MANAGER_HH */
