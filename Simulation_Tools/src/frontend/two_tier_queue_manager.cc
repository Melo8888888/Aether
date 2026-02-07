#include "two_tier_queue_manager.hh"

#include <sstream>
#include <stdexcept>

namespace cellular_emulation {

TwoTierQueueManager::TwoTierQueueManager(const TwoTierQueueConfig& config)
    : driver_queue_(nullptr),
      modem_buffer_(nullptr),
      signal_exporter_(nullptr),
      config_(config),
      last_update_time_(0),
      last_signal_export_time_(0),
      total_packets_received_(0),
      total_packets_transferred_(0),
      total_backpressure_events_(0),
      drop_callback_(nullptr)
{
    initialize();
}

TwoTierQueueManager::TwoTierQueueManager()
    : TwoTierQueueManager(TwoTierQueueConfig())
{
}

TwoTierQueueManager::~TwoTierQueueManager()
{
    /* Cleanup handled by unique_ptr */
}

void TwoTierQueueManager::initialize()
{
    /* Create driver queue */
    driver_queue_ = std::make_unique<VirtualDriverQueue>(config_.driver_queue_config);

    /* Create modem buffer */
    modem_buffer_ = std::make_unique<VirtualModemBuffer>(config_.modem_buffer_config);

    /* Setup signal exporter */
    if (config_.enable_signal_export) {
        if (config_.enable_csv_logging) {
            signal_exporter_ = SignalExporterFactory::create("multi", config_.csv_log_path);
        } else {
            signal_exporter_ = SignalExporterFactory::create("file", config_.signal_export_path);
        }
    }

    /* Wire up callbacks */
    modem_buffer_->set_backpressure_callback(
        [this](bool active) { this->handle_backpressure_change(active); }
    );

    /* Forward drop events */
    driver_queue_->set_drop_callback(
        [this](size_t packets, size_t bytes) {
            if (this->drop_callback_) {
                this->drop_callback_(packets, bytes);
            }
        }
    );
}

bool TwoTierQueueManager::receive_packet(const std::string& packet, uint64_t arrival_time)
{
    total_packets_received_++;

    /* 
     * Stage 1: Packet enters Driver Queue
     * 
     * In a real system, this corresponds to the application sending data,
     * which gets queued in the kernel's network driver transmit queue.
     */
    bool accepted = driver_queue_->enqueue(packet, arrival_time);

    if (!accepted) {
        /* 
         * Packet dropped at driver queue (tail drop)
         * This happens when the driver queue is full, which typically
         * occurs during severe backpressure from the modem.
         */
        return false;
    }

    /*
     * Stage 2: Attempt to transfer to Modem Buffer
     * 
     * Immediately try to move packets from driver queue to modem buffer.
     * In reality, this would be triggered by DMA completion interrupts.
     */
    try_transfer_packets();

    return true;
}

size_t TwoTierQueueManager::try_transfer_packets()
{
    size_t transferred = 0;

    /*
     * Transfer loop: Move packets from driver queue to modem buffer
     * 
     * This simulates the DMA transfer from host memory (where the driver
     * queue resides) to device memory (the modem's internal buffer).
     * 
     * Transfer stops when:
     * 1. Driver queue is empty, OR
     * 2. Modem buffer is full (backpressure)
     */
    while (!driver_queue_->empty() && modem_buffer_->can_accept()) {
        /* Dequeue from driver queue */
        QueuedPacket packet = driver_queue_->dequeue();

        if (packet.contents.empty()) {
            /* Unexpected: empty packet from non-empty queue */
            break;
        }

        /* Enqueue to modem buffer */
        bool accepted = modem_buffer_->enqueue(std::move(packet));

        if (!accepted) {
            /*
             * This shouldn't happen if can_accept() was true,
             * but handle gracefully by re-queuing to driver queue.
             * Note: In practice, we'd need more careful handling here.
             */
            break;
        }

        transferred++;
        total_packets_transferred_++;
    }

    return transferred;
}

QueuedPacket TwoTierQueueManager::drain_packet()
{
    /*
     * Drain a packet from modem buffer for transmission
     * 
     * This is called by the link layer when bandwidth is available
     * to transmit a packet.
     */
    QueuedPacket packet = modem_buffer_->dequeue();

    if (!packet.contents.empty()) {
        /*
         * Packet extracted successfully.
         * The modem buffer now has more space, so we should try
         * to refill it from the driver queue.
         */
        try_transfer_packets();
    }

    return packet;
}

void TwoTierQueueManager::update(uint64_t current_time)
{
    last_update_time_ = current_time;

    /* Try to transfer any pending packets */
    try_transfer_packets();

    /* Export signal periodically */
    uint64_t export_interval = config_.driver_queue_config.signal_export_interval_ms;
    if (current_time - last_signal_export_time_ >= export_interval) {
        export_signal(current_time);
        last_signal_export_time_ = current_time;
    }
}

void TwoTierQueueManager::export_signal(uint64_t current_time)
{
    if (!signal_exporter_ || !config_.enable_signal_export) {
        return;
    }

    /* Build signal */
    QueueSignal signal;
    signal.queue_length = driver_queue_->size();
    signal.head_dwell_time_ms = driver_queue_->head_dwell_time(current_time);
    signal.timestamp_ms = current_time;
    signal.queue_bytes = driver_queue_->size_bytes();
    signal.backpressure_active = is_backpressure_active();

    /* Calculate growth rate (simplified) */
    static size_t last_queue_size = 0;
    static uint64_t last_time = 0;
    if (last_time > 0 && current_time > last_time) {
        double dt = static_cast<double>(current_time - last_time) / 1000.0;
        signal.queue_growth_rate = 
            static_cast<double>(static_cast<int>(signal.queue_length) - 
                                static_cast<int>(last_queue_size)) / dt;
    }
    last_queue_size = signal.queue_length;
    last_time = current_time;

    /* Export */
    signal_exporter_->export_signal(signal);
}

void TwoTierQueueManager::export_signal_now(uint64_t current_time)
{
    export_signal(current_time);
    last_signal_export_time_ = current_time;
}

size_t TwoTierQueueManager::driver_queue_size() const
{
    return driver_queue_->size();
}

size_t TwoTierQueueManager::modem_buffer_size() const
{
    return modem_buffer_->size_packets();
}

size_t TwoTierQueueManager::total_queued_packets() const
{
    return driver_queue_->size() + modem_buffer_->size_packets();
}

bool TwoTierQueueManager::is_backpressure_active() const
{
    return modem_buffer_->backpressure_active();
}

uint64_t TwoTierQueueManager::driver_queue_dwell_time(uint64_t current_time) const
{
    return driver_queue_->head_dwell_time(current_time);
}

const VDQueueStats& TwoTierQueueManager::driver_queue_statistics() const
{
    return driver_queue_->statistics();
}

std::string TwoTierQueueManager::statistics_summary() const
{
    std::ostringstream oss;
    
    oss << "=== Two-Tier Queue Manager Statistics ===\n\n";
    
    oss << "--- Driver Queue ---\n";
    oss << driver_queue_->statistics().to_string() << "\n";
    
    oss << "--- Modem Buffer ---\n";
    oss << "  Enqueued: " << modem_buffer_->total_enqueued() << " packets\n";
    oss << "  Transmitted: " << modem_buffer_->total_dequeued() << " packets\n";
    oss << "  Rejected: " << modem_buffer_->total_rejected() << " packets\n";
    oss << "  Current size: " << modem_buffer_->size_packets() << " packets\n";
    oss << "  Utilization: " << (modem_buffer_->utilization() * 100.0) << "%\n\n";
    
    oss << "--- Overall ---\n";
    oss << "  Total received: " << total_packets_received_ << " packets\n";
    oss << "  Total transferred: " << total_packets_transferred_ << " packets\n";
    oss << "  Backpressure events: " << total_backpressure_events_ << "\n";
    
    return oss.str();
}

void TwoTierQueueManager::reset_statistics()
{
    driver_queue_->reset_statistics();
    total_packets_received_ = 0;
    total_packets_transferred_ = 0;
    total_backpressure_events_ = 0;
}

void TwoTierQueueManager::set_drop_callback(std::function<void(size_t, size_t)> callback)
{
    drop_callback_ = callback;
    driver_queue_->set_drop_callback(callback);
}

void TwoTierQueueManager::handle_backpressure_change(bool active)
{
    if (active) {
        total_backpressure_events_++;
    }
}

std::string TwoTierQueueManager::to_string() const
{
    std::ostringstream oss;
    oss << "TwoTierQueueManager{"
        << "driver=" << driver_queue_->size()
        << "/" << driver_queue_->capacity()
        << ", modem=" << modem_buffer_->size_packets()
        << ", backpressure=" << (is_backpressure_active() ? "YES" : "no")
        << "}";
    return oss.str();
}

} // namespace cellular_emulation
