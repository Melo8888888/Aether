#ifndef VIRTUAL_MODEM_BUFFER_HH
#define VIRTUAL_MODEM_BUFFER_HH

#include <queue>
#include <deque>
#include <cstdint>
#include <string>
#include <memory>
#include <functional>

#include "queued_packet.hh"
#include "vmodem_config.hh"

namespace cellular_emulation {

/**
 * @class VirtualModemBuffer
 * @brief Simulates the modem chip's internal transmission buffer
 * 
 * The VirtualModemBuffer models the hardware buffer inside the cellular modem.
 * Packets are transferred here from the driver queue via DMA, and drained
 * according to the wireless link capacity determined by the trace file.
 * 
 * When this buffer is full, it signals backpressure to the driver layer,
 * preventing further packet transfers.
 */
class VirtualModemBuffer {
public:
    /**
     * @brief Construct with configuration
     * @param config Configuration parameters
     */
    explicit VirtualModemBuffer(const VModemConfig& config);

    /**
     * @brief Default constructor
     */
    VirtualModemBuffer();

    /**
     * @brief Destructor
     */
    ~VirtualModemBuffer();

    // Disable copy
    VirtualModemBuffer(const VirtualModemBuffer&) = delete;
    VirtualModemBuffer& operator=(const VirtualModemBuffer&) = delete;

    // Enable move
    VirtualModemBuffer(VirtualModemBuffer&&) noexcept;
    VirtualModemBuffer& operator=(VirtualModemBuffer&&) noexcept;

    /**
     * @brief Enqueue a packet from driver queue (DMA transfer simulation)
     * @param packet The packet to enqueue
     * @return true if accepted, false if buffer full (backpressure)
     */
    bool enqueue(QueuedPacket&& packet);

    /**
     * @brief Dequeue a packet for transmission
     * @return The dequeued packet
     */
    QueuedPacket dequeue();

    /**
     * @brief Check if buffer can accept more packets
     * @return true if buffer has space
     */
    bool can_accept() const noexcept;

    /**
     * @brief Check if backpressure is active
     * @return true if buffer is full and cannot accept packets
     */
    bool backpressure_active() const noexcept;

    /**
     * @brief Check if buffer is empty
     * @return true if no packets in buffer
     */
    bool empty() const noexcept;

    /**
     * @brief Get current packet count
     * @return Number of packets
     */
    size_t size_packets() const noexcept;

    /**
     * @brief Get current byte count
     * @return Total bytes in buffer
     */
    size_t size_bytes() const noexcept;

    /**
     * @brief Get remaining capacity in packets
     * @return Number of packets that can still be accepted
     */
    size_t remaining_capacity() const noexcept;

    /**
     * @brief Get buffer utilization ratio
     * @return Utilization as fraction (0.0 to 1.0)
     */
    double utilization() const noexcept;

    /**
     * @brief Set callback for backpressure state changes
     * @param callback Function called when backpressure state changes
     */
    void set_backpressure_callback(std::function<void(bool)> callback);

    /**
     * @brief Get string representation
     * @return Description of buffer state
     */
    std::string to_string() const;

    /* Statistics */
    
    /** Total packets enqueued */
    uint64_t total_enqueued() const noexcept { return total_enqueued_; }
    
    /** Total packets dequeued (transmitted) */
    uint64_t total_dequeued() const noexcept { return total_dequeued_; }
    
    /** Total packets rejected due to backpressure */
    uint64_t total_rejected() const noexcept { return total_rejected_; }

private:
    /* Packet storage */
    std::deque<QueuedPacket> buffer_;

    /* Configuration */
    VModemConfig config_;

    /* State tracking */
    size_t total_bytes_;
    bool backpressure_state_;

    /* Statistics */
    uint64_t total_enqueued_;
    uint64_t total_dequeued_;
    uint64_t total_rejected_;

    /* Callbacks */
    std::function<void(bool)> backpressure_callback_;

    /* Helper methods */
    void update_backpressure_state();
};

} // namespace cellular_emulation

#endif /* VIRTUAL_MODEM_BUFFER_HH */
