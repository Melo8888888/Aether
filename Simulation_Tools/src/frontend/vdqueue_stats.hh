#ifndef VDQUEUE_STATS_HH
#define VDQUEUE_STATS_HH

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <fstream>

namespace cellular_emulation {

/**
 * @struct VDQueueStats
 * @brief Cumulative statistics for Virtual Driver Queue
 * 
 * Tracks various metrics including packet counts, byte counts,
 * dwell times, and queue utilization over time.
 */
struct VDQueueStats {
    /** Total packets successfully enqueued */
    uint64_t total_enqueued_packets = 0;
    
    /** Total packets successfully dequeued */
    uint64_t total_dequeued_packets = 0;
    
    /** Total packets dropped due to queue full */
    uint64_t total_dropped_packets = 0;
    /** Total bytes successfully enqueued */
    uint64_t total_enqueued_bytes = 0;
    
    /** Total bytes successfully dequeued */
    uint64_t total_dequeued_bytes = 0;
    
    /** Total bytes dropped */
    uint64_t total_dropped_bytes = 0;
    /** Peak queue size in packets */
    size_t peak_queue_size = 0;
    
    /** Peak queue size in bytes */
    size_t peak_queue_bytes = 0;
    /** Maximum observed dwell time (ms) */
    uint64_t max_dwell_time = 0;
    
    /** Sum of all dwell times for averaging */
    uint64_t sum_dwell_time = 0;
    
    /** Number of dwell time samples */
    uint64_t dwell_time_samples = 0;
    /** Number of times backpressure was triggered */
    uint64_t backpressure_events = 0;
    
    /** Total duration of backpressure (ms) */
    uint64_t total_backpressure_duration = 0;
    /** Timestamp when statistics collection started */
    uint64_t start_time = 0;
    
    /** Timestamp of last update */
    uint64_t last_update_time = 0;
    
    /**
     * @brief Reset all statistics to zero
     */
    void reset()
    {
        total_enqueued_packets = 0;
        total_dequeued_packets = 0;
        total_dropped_packets = 0;
        total_enqueued_bytes = 0;
        total_dequeued_bytes = 0;
        total_dropped_bytes = 0;
        peak_queue_size = 0;
        peak_queue_bytes = 0;
        max_dwell_time = 0;
        sum_dwell_time = 0;
        dwell_time_samples = 0;
        backpressure_events = 0;
        total_backpressure_duration = 0;
        start_time = 0;
        last_update_time = 0;
    }
    
    /**
     * @brief Calculate packet drop rate
     * @return Drop rate as a fraction (0.0 to 1.0)
     */
    double drop_rate() const
    {
        uint64_t total_in = total_enqueued_packets + total_dropped_packets;
        if (total_in == 0) return 0.0;
        return static_cast<double>(total_dropped_packets) / static_cast<double>(total_in);
    }
    
    /**
     * @brief Calculate average dwell time
     * @return Average dwell time in milliseconds
     */
    double average_dwell_time() const
    {
        if (dwell_time_samples == 0) return 0.0;
        return static_cast<double>(sum_dwell_time) / static_cast<double>(dwell_time_samples);
    }
    
    /**
     * @brief Calculate average packet size
     * @return Average packet size in bytes
     */
    double average_packet_size() const
    {
        if (total_enqueued_packets == 0) return 0.0;
        return static_cast<double>(total_enqueued_bytes) / 
               static_cast<double>(total_enqueued_packets);
    }
    
    /**
     * @brief Calculate throughput in bits per second
     * @return Throughput in bps
     */
    double throughput_bps() const
    {
        if (last_update_time <= start_time) return 0.0;
        double duration_seconds = static_cast<double>(last_update_time - start_time) / 1000.0;
        if (duration_seconds <= 0) return 0.0;
        return (static_cast<double>(total_dequeued_bytes) * 8.0) / duration_seconds;
    }
    
    /**
     * @brief Calculate goodput in Mbps
     * @return Goodput in Mbps
     */
    double goodput_mbps() const
    {
        return throughput_bps() / 1000000.0;
    }
    
    /**
     * @brief Generate summary string
     * @return Human-readable summary of all statistics
     */
    std::string to_string() const
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        
        oss << "=== VDQueue Statistics ===\n";
        oss << "Packets: enqueued=" << total_enqueued_packets
            << ", dequeued=" << total_dequeued_packets
            << ", dropped=" << total_dropped_packets
            << " (" << (drop_rate() * 100.0) << "% drop rate)\n";
        
        oss << "Bytes: enqueued=" << total_enqueued_bytes
            << ", dequeued=" << total_dequeued_bytes
            << ", dropped=" << total_dropped_bytes << "\n";
        
        oss << "Queue: peak_size=" << peak_queue_size
            << " pkts, peak_bytes=" << peak_queue_bytes << "\n";
        
        oss << "Dwell: max=" << max_dwell_time
            << " ms, avg=" << average_dwell_time() << " ms\n";
        
        oss << "Throughput: " << goodput_mbps() << " Mbps\n";
        
        oss << "Backpressure: events=" << backpressure_events
            << ", total_duration=" << total_backpressure_duration << " ms\n";
        
        return oss.str();
    }
    
    /**
     * @brief Export statistics to CSV format
     * @param include_header Whether to include column headers
     * @return CSV formatted string
     */
    std::string to_csv(bool include_header = false) const
    {
        std::ostringstream oss;
        
        if (include_header) {
            oss << "enqueued_pkts,dequeued_pkts,dropped_pkts,"
                << "enqueued_bytes,dequeued_bytes,dropped_bytes,"
                << "peak_size,max_dwell,avg_dwell,drop_rate,throughput_mbps\n";
        }
        
        oss << total_enqueued_packets << ","
            << total_dequeued_packets << ","
            << total_dropped_packets << ","
            << total_enqueued_bytes << ","
            << total_dequeued_bytes << ","
            << total_dropped_bytes << ","
            << peak_queue_size << ","
            << max_dwell_time << ","
            << std::fixed << std::setprecision(2) << average_dwell_time() << ","
            << std::fixed << std::setprecision(4) << drop_rate() << ","
            << std::fixed << std::setprecision(2) << goodput_mbps() << "\n";
        
        return oss.str();
    }
    
    /**
     * @brief Export statistics to JSON format
     * @return JSON formatted string
     */
    std::string to_json() const
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        
        oss << "{\n";
        oss << "  \"packets\": {\n";
        oss << "    \"enqueued\": " << total_enqueued_packets << ",\n";
        oss << "    \"dequeued\": " << total_dequeued_packets << ",\n";
        oss << "    \"dropped\": " << total_dropped_packets << "\n";
        oss << "  },\n";
        
        oss << "  \"bytes\": {\n";
        oss << "    \"enqueued\": " << total_enqueued_bytes << ",\n";
        oss << "    \"dequeued\": " << total_dequeued_bytes << ",\n";
        oss << "    \"dropped\": " << total_dropped_bytes << "\n";
        oss << "  },\n";
        
        oss << "  \"queue\": {\n";
        oss << "    \"peak_size\": " << peak_queue_size << ",\n";
        oss << "    \"peak_bytes\": " << peak_queue_bytes << "\n";
        oss << "  },\n";
        
        oss << "  \"dwell_time\": {\n";
        oss << "    \"max_ms\": " << max_dwell_time << ",\n";
        oss << "    \"avg_ms\": " << average_dwell_time() << "\n";
        oss << "  },\n";
        
        oss << "  \"performance\": {\n";
        oss << "    \"drop_rate\": " << drop_rate() << ",\n";
        oss << "    \"throughput_mbps\": " << goodput_mbps() << "\n";
        oss << "  },\n";
        
        oss << "  \"backpressure\": {\n";
        oss << "    \"events\": " << backpressure_events << ",\n";
        oss << "    \"total_duration_ms\": " << total_backpressure_duration << "\n";
        oss << "  }\n";
        
        oss << "}\n";
        
        return oss.str();
    }
    
    /**
     * @brief Save statistics to file
     * @param filepath Output file path
     * @param format Output format: "text", "csv", or "json"
     */
    void save_to_file(const std::string& filepath, const std::string& format = "text") const
    {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return;
        }
        
        if (format == "csv") {
            file << to_csv(true);
        } else if (format == "json") {
            file << to_json();
        } else {
            file << to_string();
        }
    }
    
    /**
     * @brief Merge statistics from another instance
     * @param other Statistics to merge
     */
    void merge(const VDQueueStats& other)
    {
        total_enqueued_packets += other.total_enqueued_packets;
        total_dequeued_packets += other.total_dequeued_packets;
        total_dropped_packets += other.total_dropped_packets;
        total_enqueued_bytes += other.total_enqueued_bytes;
        total_dequeued_bytes += other.total_dequeued_bytes;
        total_dropped_bytes += other.total_dropped_bytes;
        
        if (other.peak_queue_size > peak_queue_size) {
            peak_queue_size = other.peak_queue_size;
        }
        if (other.peak_queue_bytes > peak_queue_bytes) {
            peak_queue_bytes = other.peak_queue_bytes;
        }
        if (other.max_dwell_time > max_dwell_time) {
            max_dwell_time = other.max_dwell_time;
        }
        
        sum_dwell_time += other.sum_dwell_time;
        dwell_time_samples += other.dwell_time_samples;
        backpressure_events += other.backpressure_events;
        total_backpressure_duration += other.total_backpressure_duration;
    }
};

/**
 * @class VDQueueStatsCollector
 * @brief Time-series statistics collector for detailed analysis
 * 
 * Collects snapshots of queue state at regular intervals
 * for post-experiment analysis.
 */
class VDQueueStatsCollector {
public:
    /**
     * @struct Snapshot
     * @brief A single point-in-time measurement
     */
    struct Snapshot {
        uint64_t timestamp;
        size_t queue_size;
        size_t queue_bytes;
        uint64_t dwell_time;
        bool backpressure_active;
    };
    
    /**
     * @brief Construct collector with specified interval
     * @param interval_ms Collection interval in milliseconds
     */
    explicit VDQueueStatsCollector(uint64_t interval_ms = 10)
        : interval_ms_(interval_ms),
          last_collection_time_(0),
          snapshots_()
    {
        snapshots_.reserve(10000);  /* Pre-allocate for efficiency */
    }
    
    /**
     * @brief Record a snapshot if interval has elapsed
     * @param timestamp Current time
     * @param queue_size Current queue size
     * @param queue_bytes Current queue bytes
     * @param dwell_time Current head dwell time
     * @param backpressure Whether backpressure is active
     */
    void maybe_collect(uint64_t timestamp, size_t queue_size, 
                       size_t queue_bytes, uint64_t dwell_time,
                       bool backpressure)
    {
        if (timestamp - last_collection_time_ >= interval_ms_) {
            snapshots_.push_back({
                timestamp,
                queue_size,
                queue_bytes,
                dwell_time,
                backpressure
            });
            last_collection_time_ = timestamp;
        }
    }
    
    /**
     * @brief Get all collected snapshots
     * @return Vector of snapshots
     */
    const std::vector<Snapshot>& snapshots() const
    {
        return snapshots_;
    }
    
    /**
     * @brief Clear all collected snapshots
     */
    void clear()
    {
        snapshots_.clear();
        last_collection_time_ = 0;
    }
    
    /**
     * @brief Export snapshots to CSV file
     * @param filepath Output file path
     */
    void export_csv(const std::string& filepath) const
    {
        std::ofstream file(filepath);
        if (!file.is_open()) return;
        
        file << "timestamp_ms,queue_size,queue_bytes,dwell_time_ms,backpressure\n";
        
        for (const auto& s : snapshots_) {
            file << s.timestamp << ","
                 << s.queue_size << ","
                 << s.queue_bytes << ","
                 << s.dwell_time << ","
                 << (s.backpressure_active ? 1 : 0) << "\n";
        }
    }
    
private:
    uint64_t interval_ms_;
    uint64_t last_collection_time_;
    std::vector<Snapshot> snapshots_;
};

} // namespace cellular_emulation

#endif /* VDQUEUE_STATS_HH */
