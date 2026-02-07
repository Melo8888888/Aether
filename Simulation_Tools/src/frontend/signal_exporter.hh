#ifndef SIGNAL_EXPORTER_HH
#define SIGNAL_EXPORTER_HH

#include <cstdint>
#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <atomic>
#include <mutex>

namespace cellular_emulation {

/**
 * @struct QueueSignal
 * @brief Queue state signal for CCA consumption
 * 
 * This structure contains the essential queue state information
 * that congestion control algorithms can use to infer network conditions.
 */
struct QueueSignal {
    /** Current number of packets in driver queue */
    size_t queue_length = 0;
    
    /** Dwell time of head packet in milliseconds */
    uint64_t head_dwell_time_ms = 0;
    
    /** Timestamp when signal was captured */
    uint64_t timestamp_ms = 0;
    
    /** Rate of change of queue length (packets per second) */
    double queue_growth_rate = 0.0;
    
    /** Whether backpressure is currently active */
    bool backpressure_active = false;
    
    /** Total bytes in driver queue */
    size_t queue_bytes = 0;
    
    /**
     * @brief Format as simple space-separated string
     * @return "queue_length dwell_time_ms"
     */
    std::string to_simple_string() const
    {
        std::ostringstream oss;
        oss << queue_length << " " << head_dwell_time_ms;
        return oss.str();
    }
    
    /**
     * @brief Format as detailed string
     * @return Full signal description
     */
    std::string to_string() const
    {
        std::ostringstream oss;
        oss << "QueueSignal{"
            << "len=" << queue_length
            << ", dwell=" << head_dwell_time_ms << "ms"
            << ", ts=" << timestamp_ms
            << ", backpressure=" << (backpressure_active ? "Y" : "N")
            << "}";
        return oss.str();
    }
    
    /**
     * @brief Format as CSV line
     * @return CSV formatted string
     */
    std::string to_csv_line() const
    {
        std::ostringstream oss;
        oss << timestamp_ms << ","
            << queue_length << ","
            << head_dwell_time_ms << ","
            << queue_bytes << ","
            << queue_growth_rate << ","
            << (backpressure_active ? 1 : 0);
        return oss.str();
    }
};

/**
 * @class SignalExporter
 * @brief Abstract base class for signal exporters
 */
class SignalExporter {
public:
    virtual ~SignalExporter() = default;
    
    /**
     * @brief Export a queue signal
     * @param signal The signal to export
     * @return true if export succeeded
     */
    virtual bool export_signal(const QueueSignal& signal) = 0;
    
    /**
     * @brief Get exporter type name
     * @return Type description
     */
    virtual std::string type() const = 0;
    
    /**
     * @brief Check if exporter is ready
     * @return true if ready to export
     */
    virtual bool is_ready() const = 0;
};

/**
 * @class FileSignalExporter
 * @brief Export signals to a file (default method)
 * 
 * Writes queue state to a file that can be read by external processes.
 * Uses atomic file updates to ensure consistent reads.
 */
class FileSignalExporter : public SignalExporter {
public:
    /**
     * @brief Construct with file path
     * @param filepath Path to signal file
     * @param format Output format: "simple" or "full"
     */
    explicit FileSignalExporter(const std::string& filepath,
                                const std::string& format = "simple")
        : filepath_(filepath),
          format_(format),
          min_interval_ms_(1),
          last_export_time_(0),
          export_count_(0),
          mutex_()
    {
    }
    
    /**
     * @brief Export signal to file
     * @param signal The signal to export
     * @return true if written successfully
     */
    bool export_signal(const QueueSignal& signal) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        /* Rate limiting */
        if (signal.timestamp_ms - last_export_time_ < min_interval_ms_) {
            return true;  /* Skip but don't report as error */
        }
        
        /* Open file for writing (truncate) */
        std::ofstream file(filepath_);
        if (!file.is_open()) {
            return false;
        }
        
        /* Write signal based on format */
        if (format_ == "simple") {
            file << signal.to_simple_string() << std::endl;
        } else {
            file << signal.to_string() << std::endl;
        }
        
        last_export_time_ = signal.timestamp_ms;
        export_count_++;
        
        return true;
    }
    
    std::string type() const override { return "file"; }
    
    bool is_ready() const override { return true; }
    
    /**
     * @brief Set minimum interval between exports
     * @param interval_ms Minimum interval in milliseconds
     */
    void set_min_interval(uint64_t interval_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        min_interval_ms_ = interval_ms;
    }
    
    /**
     * @brief Get export count
     * @return Number of successful exports
     */
    uint64_t export_count() const { return export_count_; }

private:
    std::string filepath_;
    std::string format_;
    uint64_t min_interval_ms_;
    uint64_t last_export_time_;
    std::atomic<uint64_t> export_count_;
    std::mutex mutex_;
};

/**
 * @class CSVSignalLogger
 * @brief Log all signals to CSV file for post-analysis
 * 
 * Unlike FileSignalExporter which overwrites with latest value,
 * this logger appends all signals for complete time-series analysis.
 */
class CSVSignalLogger : public SignalExporter {
public:
    /**
     * @brief Construct with output file path
     * @param filepath Path to CSV log file
     * @param write_header Whether to write CSV header
     */
    explicit CSVSignalLogger(const std::string& filepath,
                             bool write_header = true)
        : filepath_(filepath),
          file_(filepath, std::ios::out | std::ios::app),
          signal_count_(0),
          mutex_()
    {
        if (write_header && file_.is_open()) {
            file_ << "timestamp_ms,queue_length,dwell_time_ms,"
                  << "queue_bytes,growth_rate,backpressure\n";
        }
    }
    
    ~CSVSignalLogger() override
    {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    bool export_signal(const QueueSignal& signal) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!file_.is_open()) {
            return false;
        }
        
        file_ << signal.to_csv_line() << "\n";
        signal_count_++;
        
        /* Periodic flush for durability */
        if (signal_count_ % 100 == 0) {
            file_.flush();
        }
        
        return true;
    }
    
    std::string type() const override { return "csv_logger"; }
    
    bool is_ready() const override { return file_.is_open(); }
    
    /**
     * @brief Force flush to disk
     */
    void flush()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }
    
    /**
     * @brief Get number of logged signals
     * @return Signal count
     */
    uint64_t signal_count() const { return signal_count_; }

private:
    std::string filepath_;
    std::ofstream file_;
    std::atomic<uint64_t> signal_count_;
    std::mutex mutex_;
};

/**
 * @class MultiExporter
 * @brief Export to multiple destinations simultaneously
 * 
 * Useful for both real-time CCA integration and post-analysis logging.
 */
class MultiExporter : public SignalExporter {
public:
    MultiExporter() : exporters_() {}
    
    /**
     * @brief Add an exporter to the chain
     * @param exporter Exporter to add
     */
    void add_exporter(std::shared_ptr<SignalExporter> exporter)
    {
        exporters_.push_back(exporter);
    }
    
    bool export_signal(const QueueSignal& signal) override
    {
        bool all_success = true;
        for (auto& exporter : exporters_) {
            if (!exporter->export_signal(signal)) {
                all_success = false;
            }
        }
        return all_success;
    }
    
    std::string type() const override { return "multi"; }
    
    bool is_ready() const override
    {
        for (const auto& exporter : exporters_) {
            if (!exporter->is_ready()) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * @brief Get number of configured exporters
     * @return Exporter count
     */
    size_t exporter_count() const { return exporters_.size(); }

private:
    std::vector<std::shared_ptr<SignalExporter>> exporters_;
};

/**
 * @class SignalExporterFactory
 * @brief Factory for creating signal exporters
 */
class SignalExporterFactory {
public:
    /**
     * @brief Create exporter based on type string
     * @param type_str Type: "file", "csv", or "multi"
     * @param config Configuration string (path for file/csv)
     * @return Configured exporter
     */
    static std::shared_ptr<SignalExporter> create(
        const std::string& type_str,
        const std::string& config = "/tmp/mm_virtual_driver_queue")
    {
        if (type_str == "file") {
            return std::make_shared<FileSignalExporter>(config);
        } else if (type_str == "csv") {
            return std::make_shared<CSVSignalLogger>(config);
        } else if (type_str == "multi") {
            auto multi = std::make_shared<MultiExporter>();
            /* Add default file exporter */
            multi->add_exporter(std::make_shared<FileSignalExporter>("/tmp/mm_virtual_driver_queue"));
            /* Add CSV logger */
            multi->add_exporter(std::make_shared<CSVSignalLogger>(config.empty() ? 
                "/tmp/mm_vdqueue_log.csv" : config));
            return multi;
        }
        
        /* Default to file exporter */
        return std::make_shared<FileSignalExporter>(config);
    }
};

} // namespace cellular_emulation

#endif /* SIGNAL_EXPORTER_HH */
