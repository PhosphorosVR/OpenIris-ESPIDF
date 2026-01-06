#pragma once
/**
 * @file MonitoringManager.hpp
 * @brief High-level Coordinator - Combines Battery and Current monitoring
 *
 * Architecture:
 * +-----------------------+
 * |   MonitoringManager   | ← High-level coordinator (this file)
 * +-----------------------+
 * |  BatteryMonitor       | ← Battery logic: voltage, capacity, health
 * |  CurrentMonitor       | ← Current logic: power, instantaneous current
 * +-----------------------+
 * |      AdcSampler       | ← BSP: Unified ADC sampling interface
 * +-----------------------+
 * |   ESP-IDF ADC HAL     | ← Espressif official driver
 * +-----------------------+
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <mutex>
#include "BatteryMonitor.hpp"
#include "CurrentMonitor.hpp"

/**
 * @class MonitoringManager
 * @brief Coordinates battery and current monitoring subsystems
 *
 * This class manages the lifecycle and periodic sampling of both
 * BatteryMonitor and CurrentMonitor. It runs a background FreeRTOS task
 * to perform periodic measurements based on Kconfig intervals.
 *
 * Thread-safety: Uses atomic variables for cross-thread data access.
 */
class MonitoringManager
{
   public:
    MonitoringManager() = default;
    ~MonitoringManager() = default;

    // Initialize monitoring subsystems based on Kconfig settings
    void setup();
    // Start the background monitoring task
    void start();
    // Stop the background monitoring task
    void stop();

    // Latest filtered current in mA
    float getCurrentMilliAmps() const;
    // Get complete battery status (voltage + percentage + validity)
    BatteryStatus getBatteryStatus() const;

    // Check if any monitoring feature is enabled
    static constexpr bool isEnabled()
    {
        return CurrentMonitor::isEnabled() || BatteryMonitor::isEnabled();
    }

   private:
    static void taskEntry(void* arg);
    void run();

    TaskHandle_t task_{nullptr};
    std::atomic<float> last_current_ma_{0.0f};
    BatteryStatus last_battery_status_{0, 0.0f, false};
    mutable std::mutex battery_mutex_;  // Protect non-atomic BatteryStatus

    CurrentMonitor cm_;
    BatteryMonitor bm_;
};
