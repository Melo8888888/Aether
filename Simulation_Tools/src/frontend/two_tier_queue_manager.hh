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

/**
 * @struct TwoTierQueueConfig
 * @brief Configuration for the two-tier queue system
 */
struct TwoTierQueueConfig {
    VDQueueConfig driver_queue_config;
    VModemConfig modem_buffer_config;
    std::string signal_export_path = "/tmp/mm_virtual_driver_queue";
    bool enable_signal_export = true;
    bool enable_csv_logging = false;
    std::string csv_log_path = "/tmp/mm_vdqueue_log.csv";
    
    TwoTierQueueConfig() = default;
};

/**
 * @class TwoTierQueueManager
 * @brief Manages the two-tier queue architecture
 * 
 * This class integrates the Virtual Driver Queue and Virtual Modem Buffer
 * into a cohesive system that accurately models cellular uplink buffering.
 * 
 * Packet Flow:
 * Application -> VDQueue (driver layer) -> VModemBuf (modem layer) -> Link
 * 
 * Backpressure Flow:
 * Link <- VModemBuf backpressure <- VDQueue buildup <- Application slowdown
 */
class TwoTierQueueManager {
public:
    /**
     * @brief Construct with configuration
     * @param config Configuration for both queues
     */
    explicit TwoTierQueueManager(const TwoTierQueueConfig& config);

    /**
     * @brief Default constructor
     */
    TwoTierQueueManager();

    /**
     * @brief Destructor
     */
    ~TwoTierQueueManager();

    // Disable copy
    TwoTierQueueManager(const TwoTierQueueManager&) = delete;
    TwoTierQueueManager& operator=(const TwoTierQueueManager&) = delete;

    /**
     * @brief Receive a packet from the application layer
     * 
     * The packet enters the driver queue (VDQueue).
     * If the driver queue is full, the packet is dropped.
     * 
     * @param packet Packet contents
     * @param arrival_time Arrival timestamp
     * @return true if packet was accepted
     */
    bool receive_packet(const std::string& packet, uint64_t arrival_time);

    /**
     * @brief Attempt to transfer packets from driver queue to modem buffer
     * 
     * Simulates DMA transfer from host memory (driver) to device memory (modem).
     * Transfer is blocked if modem buffer is full (backpressure).
     * 
     * @return Number of packets transferred
     */
    size_t try_transfer_packets();

    /**
     * @brief Drain a packet from modem buffer for transmission
     * 
     * Called when the link is ready to transmit a packet.
     * 
     * @return The packet to transmit, or empty if buffer is empty
     */
    QueuedPacket drain_packet();

    /**
     * @brief Update queue state and export signals
     * 
     * Should be called periodically to update statistics and export signals.
     * 
     * @param current_time Current timestamp
     */
    void update(uint64_t current_time);

    /**
     * @brief Get current driver queue size
     * @return Number of packets in driver queue
     */
    size_t driver_queue_size() const;

    /**
     * @brief Get current modem buffer size
     * @return Number of packets in modem buffer
     */
    size_t modem_buffer_size() const;

    /**
     * @brief Get total packets across both tiers
     * @return Total packet count
     */
    size_t total_queued_packets() const;

    /**
     * @brief Check if driver queue is experiencing backpressure
     * @return true if modem buffer is full
     */
    bool is_backpressure_active() const;

    /**
     * @brief Get head dwell time in driver queue
     * @param current_time Current timestamp
     * @return Dwell time in milliseconds
     */
    uint64_t driver_queue_dwell_time(uint64_t current_time) const;

    /**
     * @brief Get driver queue statistics
     * @return Reference to statistics object
     */
    const VDQueueStats& driver_queue_statistics() const;

    /**
     * @brief Get combined statistics as string
     * @return Statistics summary
     */
    std::string statistics_summary() const;

    /**
     * @brief Reset all statistics
     */
    void reset_statistics();

    /**
     * @brief Export current signal immediately
     * @param current_time Current timestamp
     */
    void export_signal_now(uint64_t current_time);

    /**
     * @brief Set callback for drop events
     * @param callback Function called when packet is dropped
     */
    void set_drop_callback(std::function<void(size_t, size_t)> callback);

    /**
     * @brief Get string representation
     * @return Description of manager state
     */
    std::string to_string() const;

private:
    /* Two-tier queue components */
    std::unique_ptr<VirtualDriverQueue> driver_queue_;
    std::unique_ptr<VirtualModemBuffer> modem_buffer_;

    /* Signal exporter */
    std::shared_ptr<SignalExporter> signal_exporter_;

    /* Configuration */
    TwoTierQueueConfig config_;

    /* State tracking */
    uint64_t last_update_time_;
    uint64_t last_signal_export_time_;

    /* Statistics */
    uint64_t total_packets_received_;
    uint64_t total_packets_transferred_;
    uint64_t total_backpressure_events_;

    /* Callbacks */
    std::function<void(size_t, size_t)> drop_callback_;

    /* Helper methods */
    void initialize();
    void export_signal(uint64_t current_time);
    void handle_backpressure_change(bool active);
};

} // namespace cellular_emulation

#endif /* TWO_TIER_QUEUE_MANAGER_HH */
