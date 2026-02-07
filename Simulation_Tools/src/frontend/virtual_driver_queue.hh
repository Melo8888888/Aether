#ifndef VIRTUAL_DRIVER_QUEUE_HH
#define VIRTUAL_DRIVER_QUEUE_HH

#include <deque>
#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

#include "queued_packet.hh"
#include "vdqueue_config.hh"
#include "vdqueue_stats.hh"

namespace cellular_emulation {

/**
 * @class VirtualDriverQueue
 * @brief Simulates the network driver layer queue in cellular uplink
 * 
 * The VirtualDriverQueue models the buffering behavior at the OS kernel level,
 * specifically the network driver's transmit queue. This is the first stage
 * of buffering before packets reach the modem hardware.
 * 
 * Backpressure Mechanism:
 * When the downstream modem buffer is full, the driver queue experiences
 * backpressure - packets cannot be transferred and must wait, causing
 * queue buildup and increased dwell times.
 */
class VirtualDriverQueue {
public:
    /**
     * @brief Construct a new Virtual Driver Queue
     * @param config Configuration parameters for the queue
     */
    explicit VirtualDriverQueue(const VDQueueConfig& config);

    /**
     * @brief Default constructor with default configuration
     */
    VirtualDriverQueue();

    /**
     * @brief Destructor
     */
    ~VirtualDriverQueue();

    // Disable copy semantics
    VirtualDriverQueue(const VirtualDriverQueue&) = delete;
    VirtualDriverQueue& operator=(const VirtualDriverQueue&) = delete;

    // Enable move semantics
    VirtualDriverQueue(VirtualDriverQueue&&) noexcept;
    VirtualDriverQueue& operator=(VirtualDriverQueue&&) noexcept;

    /**
     * @brief Enqueue a packet into the driver queue
     * @param packet The packet to enqueue
     * @param arrival_time Timestamp when packet arrived
     * @return true if packet was enqueued, false if dropped due to full queue
     */
    bool enqueue(const std::string& packet, uint64_t arrival_time);

    /**
     * @brief Enqueue a packet using move semantics
     * @param packet The packet to enqueue (moved)
     * @param arrival_time Timestamp when packet arrived
     * @return true if packet was enqueued, false if dropped
     */
    bool enqueue(std::string&& packet, uint64_t arrival_time);

    /**
     * @brief Dequeue a packet from the front of the queue
     * @return The dequeued packet, or empty packet if queue is empty
     */
    QueuedPacket dequeue();

    /**
     * @brief Peek at the front packet without removing it
     * @return Reference to the front packet
     * @throws std::runtime_error if queue is empty
     */
    const QueuedPacket& front() const;

    /**
     * @brief Check if the queue is empty
     * @return true if queue has no packets
     */
    bool empty() const noexcept;

    /**
     * @brief Check if the queue is full
     * @return true if queue has reached maximum capacity
     */
    bool full() const noexcept;

    /**
     * @brief Get current number of packets in queue
     * @return Number of packets
     */
    size_t size() const noexcept;

    /**
     * @brief Get total bytes of all packets in queue
     * @return Total bytes
     */
    size_t size_bytes() const noexcept;

    /**
     * @brief Get the maximum queue capacity in packets
     * @return Maximum capacity
     */
    size_t capacity() const noexcept;

    /**
     * @brief Calculate dwell time of the head packet
     * @param current_time Current timestamp
     * @return Dwell time in milliseconds, 0 if queue is empty
     */
    uint64_t head_dwell_time(uint64_t current_time) const;

    /**
     * @brief Calculate average dwell time of all packets
     * @param current_time Current timestamp
     * @return Average dwell time in milliseconds
     */
    double average_dwell_time(uint64_t current_time) const;

    /**
     * @brief Get cumulative statistics
     * @return Reference to statistics object
     */
    const VDQueueStats& statistics() const noexcept;

    /**
     * @brief Reset statistics counters
     */
    void reset_statistics();

    /**
     * @brief Set callback for drop events
     * @param callback Function to call when packet is dropped
     */
    void set_drop_callback(std::function<void(size_t, size_t)> callback);

    /**
     * @brief Set callback for enqueue events
     * @param callback Function to call when packet is enqueued
     */
    void set_enqueue_callback(std::function<void(uint64_t, size_t)> callback);

    /**
     * @brief Get string representation for logging
     * @return String description of queue state
     */
    std::string to_string() const;

    /**
     * @brief Update configuration at runtime
     * @param config New configuration
     */
    void update_config(const VDQueueConfig& config);

private:
    /* Internal packet storage */
    std::deque<QueuedPacket> queue_;

    /* Configuration */
    VDQueueConfig config_;

    /* Statistics */
    VDQueueStats stats_;

    /* Total bytes currently in queue */
    size_t total_bytes_;

    /* Callbacks */
    std::function<void(size_t, size_t)> drop_callback_;
    std::function<void(uint64_t, size_t)> enqueue_callback_;

    /* Thread safety (optional, for future multi-threaded support) */
    mutable std::mutex mutex_;

    /* Helper methods */
    void record_enqueue(uint64_t time, size_t bytes);
    void record_drop(size_t packets, size_t bytes);
    void update_statistics(uint64_t current_time);
};

} // namespace cellular_emulation

#endif /* VIRTUAL_DRIVER_QUEUE_HH */
