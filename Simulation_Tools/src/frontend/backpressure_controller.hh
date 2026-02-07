#ifndef BACKPRESSURE_CONTROLLER_HH
#define BACKPRESSURE_CONTROLLER_HH

#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <deque>
#include <memory>
#include <sstream>
#include <cmath>

namespace cellular_emulation {

/**
 * @enum BackpressurePolicy
 * @brief Available backpressure policies
 */
enum class BackpressurePolicy {
    /** Simple threshold-based policy */
    THRESHOLD,
    
    /** Threshold with hysteresis to prevent oscillation */
    HYSTERESIS,
    
    /** Predictive policy based on queue growth rate */
    PREDICTIVE,
    
    /** Adaptive policy that adjusts based on traffic patterns */
    ADAPTIVE
};

/**
 * @struct BackpressureConfig
 * @brief Configuration for backpressure controller
 */
struct BackpressureConfig {
    /** Policy to use */
    BackpressurePolicy policy = BackpressurePolicy::HYSTERESIS;
    
    /** High threshold - activate backpressure above this (fraction) */
    double high_threshold = 1.0;
    
    /** Low threshold - release backpressure below this (fraction) */
    double low_threshold = 0.8;
    
    /** Prediction horizon (ms) for predictive policy */
    uint64_t prediction_horizon_ms = 100;
    
    /** Window size for rate estimation */
    size_t rate_estimation_window = 10;
    
    /** Minimum interval between state changes (ms) */
    uint64_t min_state_duration_ms = 5;
    
    /** Enable predictive early activation */
    bool enable_early_activation = false;
    
    /**
     * @brief Validate configuration
     * @return true if valid
     */
    bool validate() const
    {
        if (high_threshold < 0.0 || high_threshold > 1.0) return false;
        if (low_threshold < 0.0 || low_threshold > high_threshold) return false;
        return true;
    }
    
    std::string to_string() const
    {
        std::ostringstream oss;
        oss << "BackpressureConfig{"
            << "policy=" << static_cast<int>(policy)
            << ", high=" << high_threshold
            << ", low=" << low_threshold
            << "}";
        return oss.str();
    }
};

/**
 * @struct BackpressureState
 * @brief Current state of backpressure controller
 */
struct BackpressureState {
    /** Whether backpressure is currently active */
    bool active = false;
    
    /** Time when current state started */
    uint64_t state_start_time = 0;
    
    /** Current buffer utilization */
    double utilization = 0.0;
    
    /** Estimated queue growth rate (packets/second) */
    double growth_rate = 0.0;
    
    /** Predicted time until full (ms), -1 if shrinking */
    int64_t predicted_time_to_full_ms = -1;
    
    /** Number of state transitions */
    uint64_t transition_count = 0;
};

/**
 * @class BackpressureController
 * @brief Manages backpressure signaling between queue tiers
 * 
 * Provides flexible backpressure management with multiple policies:
 * 
 * - THRESHOLD: Simple on/off at a single threshold
 * - HYSTERESIS: Two thresholds to prevent oscillation
 * - PREDICTIVE: Activate early based on queue growth prediction
 * - ADAPTIVE: Self-tuning based on observed traffic patterns
 */
class BackpressureController {
public:
    /**
     * @brief Construct with configuration
     * @param config Controller configuration
     */
    explicit BackpressureController(const BackpressureConfig& config)
        : config_(config),
          state_(),
          utilization_history_(),
          state_change_callback_(nullptr)
    {
        /* deque doesn't need reserve, it handles dynamic sizing */
    }
    
    /**
     * @brief Default constructor
     */
    BackpressureController() : BackpressureController(BackpressureConfig()) {}
    
    /**
     * @brief Update controller with current buffer state
     * @param current_size Current buffer size
     * @param max_size Maximum buffer size
     * @param current_time Current timestamp (ms)
     * @return true if backpressure is active
     */
    bool update(size_t current_size, size_t max_size, uint64_t current_time)
    {
        /* Calculate utilization */
        double utilization = (max_size > 0) ? 
            static_cast<double>(current_size) / static_cast<double>(max_size) : 0.0;
        
        state_.utilization = utilization;
        
        /* Update utilization history for rate estimation */
        update_utilization_history(utilization, current_time);
        
        /* Estimate growth rate */
        state_.growth_rate = estimate_growth_rate();
        
        /* Predict time to full */
        if (state_.growth_rate > 0) {
            double remaining = config_.high_threshold - utilization;
            if (remaining > 0) {
                state_.predicted_time_to_full_ms = 
                    static_cast<int64_t>(remaining / state_.growth_rate * 1000.0);
            } else {
                state_.predicted_time_to_full_ms = 0;
            }
        } else {
            state_.predicted_time_to_full_ms = -1;
        }
        
        /* Determine new state based on policy */
        bool new_active = evaluate_policy(utilization, current_time);
        
        /* Check minimum state duration */
        if (new_active != state_.active) {
            uint64_t state_duration = current_time - state_.state_start_time;
            if (state_duration >= config_.min_state_duration_ms) {
                transition_state(new_active, current_time);
            }
        }
        
        return state_.active;
    }
    
    /**
     * @brief Check if backpressure is currently active
     * @return true if active
     */
    bool is_active() const { return state_.active; }
    
    /**
     * @brief Get current state
     * @return Reference to state
     */
    const BackpressureState& state() const { return state_; }
    
    /**
     * @brief Set callback for state changes
     * @param callback Function to call on state transition
     */
    void set_state_change_callback(std::function<void(bool)> callback)
    {
        state_change_callback_ = callback;
    }
    
    /**
     * @brief Reset controller state
     */
    void reset()
    {
        state_ = BackpressureState();
        utilization_history_.clear();
    }
    
    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void update_config(const BackpressureConfig& config)
    {
        config_ = config;
    }
    
    /**
     * @brief Get statistics summary
     * @return Statistics string
     */
    std::string statistics_summary() const
    {
        std::ostringstream oss;
        oss << "BackpressureController Stats:\n";
        oss << "  Active: " << (state_.active ? "YES" : "no") << "\n";
        oss << "  Utilization: " << (state_.utilization * 100.0) << "%\n";
        oss << "  Growth rate: " << state_.growth_rate << "/s\n";
        oss << "  Transitions: " << state_.transition_count << "\n";
        if (state_.predicted_time_to_full_ms >= 0) {
            oss << "  Time to full: " << state_.predicted_time_to_full_ms << " ms\n";
        }
        return oss.str();
    }

private:
    BackpressureConfig config_;
    BackpressureState state_;
    
    /** History of (timestamp, utilization) pairs for rate estimation */
    struct UtilizationSample {
        uint64_t timestamp;
        double utilization;
    };
    std::deque<UtilizationSample> utilization_history_;
    
    std::function<void(bool)> state_change_callback_;
    
    /**
     * @brief Update utilization history
     */
    void update_utilization_history(double utilization, uint64_t timestamp)
    {
        utilization_history_.push_back({timestamp, utilization});
        
        /* Keep only recent samples */
        while (utilization_history_.size() > config_.rate_estimation_window) {
            utilization_history_.pop_front();
        }
    }
    
    /**
     * @brief Estimate queue growth rate
     * @return Growth rate (utilization per second)
     */
    double estimate_growth_rate() const
    {
        if (utilization_history_.size() < 2) {
            return 0.0;
        }
        
        /* Linear regression on recent samples */
        const auto& first = utilization_history_.front();
        const auto& last = utilization_history_.back();
        
        double dt = static_cast<double>(last.timestamp - first.timestamp) / 1000.0;
        if (dt <= 0) {
            return 0.0;
        }
        
        double du = last.utilization - first.utilization;
        return du / dt;
    }
    
    /**
     * @brief Evaluate policy to determine backpressure state
     */
    bool evaluate_policy(double utilization, uint64_t current_time)
    {
        switch (config_.policy) {
            case BackpressurePolicy::THRESHOLD:
                return utilization >= config_.high_threshold;
                
            case BackpressurePolicy::HYSTERESIS:
                if (state_.active) {
                    /* Currently active: deactivate below low threshold */
                    return utilization >= config_.low_threshold;
                } else {
                    /* Currently inactive: activate above high threshold */
                    return utilization >= config_.high_threshold;
                }
                
            case BackpressurePolicy::PREDICTIVE:
                return evaluate_predictive_policy(utilization);
                
            case BackpressurePolicy::ADAPTIVE:
                return evaluate_adaptive_policy(utilization, current_time);
                
            default:
                return utilization >= config_.high_threshold;
        }
    }
    
    /**
     * @brief Evaluate predictive policy
     */
    bool evaluate_predictive_policy(double utilization)
    {
        if (utilization >= config_.high_threshold) {
            return true;
        }
        
        /* Early activation based on prediction */
        if (config_.enable_early_activation && state_.growth_rate > 0) {
            if (state_.predicted_time_to_full_ms >= 0 &&
                static_cast<uint64_t>(state_.predicted_time_to_full_ms) < config_.prediction_horizon_ms) {
                return true;
            }
        }
        
        /* Hysteresis for release */
        if (state_.active && utilization >= config_.low_threshold) {
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Evaluate adaptive policy
     */
    bool evaluate_adaptive_policy(double utilization, uint64_t /* current_time */)
    {
        /* 
         * Adaptive policy adjusts thresholds based on observed patterns.
         * For simplicity, we use hysteresis with dynamic thresholds.
         */
        double effective_high = config_.high_threshold;
        double effective_low = config_.low_threshold;
        
        /* If queue is growing fast, lower thresholds */
        if (state_.growth_rate > 1.0) {
            effective_high *= 0.9;
            effective_low *= 0.9;
        }
        
        /* If queue is shrinking, raise thresholds */
        if (state_.growth_rate < -0.5) {
            effective_high = std::min(1.0, effective_high * 1.1);
            effective_low = std::min(effective_high, effective_low * 1.1);
        }
        
        if (state_.active) {
            return utilization >= effective_low;
        } else {
            return utilization >= effective_high;
        }
    }
    
    /**
     * @brief Transition to new state
     */
    void transition_state(bool new_active, uint64_t current_time)
    {
        state_.active = new_active;
        state_.state_start_time = current_time;
        state_.transition_count++;
        
        if (state_change_callback_) {
            state_change_callback_(new_active);
        }
    }
};

} // namespace cellular_emulation

#endif /* BACKPRESSURE_CONTROLLER_HH */
