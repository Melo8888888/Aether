#ifndef VMODEM_CONFIG_HH
#define VMODEM_CONFIG_HH

#include <cstdint>
#include <string>
#include <sstream>

namespace cellular_emulation {

/**
 * @struct VModemConfig
 * @brief Configuration for Virtual Modem Transmission Buffer
 */
struct VModemConfig {
    /**
     * @brief Maximum number of packets the modem buffer can hold
     * 
     * This represents the hardware limitation of the modem chip's
     * internal transmission buffer.
     * Default: 100 packets (based on typical modem hardware specs)
     */
    size_t max_buffer_size = 100;

    /**
     * @brief Maximum total bytes (0 = unlimited)
     * Default: 0
     */
    size_t max_buffer_bytes = 0;

    /**
     * @brief Hysteresis threshold for backpressure release
     * 
     * Backpressure is released when buffer size falls below
     * (max_buffer_size * hysteresis_ratio).
     * This prevents oscillation at the boundary.
     * Default: 0.8 (release when 80% full)
     */
    double hysteresis_ratio = 0.8;

    /**
     * @brief Enable transmission scheduling optimization
     * Default: true
     */
    bool enable_scheduling = true;

    /**
     * @brief Minimum transmission batch size
     * 
     * For efficiency, modem may wait to accumulate this many
     * packets before initiating transmission.
     * Default: 1 (no batching)
     */
    size_t min_batch_size = 1;

    /**
     * @brief Default constructor
     */
    VModemConfig() = default;

    /**
     * @brief Get string representation
     */
    std::string to_string() const
    {
        std::ostringstream oss;
        oss << "VModemConfig{"
            << "max_buffer_size=" << max_buffer_size
            << ", max_buffer_bytes=" << max_buffer_bytes
            << ", hysteresis=" << hysteresis_ratio
            << "}";
        return oss.str();
    }

    /**
     * @brief Validate configuration
     * @return true if valid
     */
    bool validate() const
    {
        if (max_buffer_size == 0) return false;
        if (hysteresis_ratio < 0.0 || hysteresis_ratio > 1.0) return false;
        return true;
    }
};

} // namespace cellular_emulation

#endif /* VMODEM_CONFIG_HH */
