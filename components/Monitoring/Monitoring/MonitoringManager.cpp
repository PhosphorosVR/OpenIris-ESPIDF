/**
 * @file MonitoringManager.cpp
 * @brief High-level Coordinator - Monitoring manager implementation
 *
 * Platform-independent monitoring coordination logic.
 * Manages BatteryMonitor and CurrentMonitor subsystems.
 */

#include "MonitoringManager.hpp"
#include <esp_log.h>
#include "sdkconfig.h"

static const char* TAG = "[MonitoringManager]";

void MonitoringManager::setup()
{
#if CONFIG_MONITORING_LED_CURRENT
    if (CurrentMonitor::isEnabled())
    {
        cm_.setup();
        ESP_LOGI(TAG, "LED current monitoring enabled. Interval=%dms, Samples=%d, Gain=%d, R=%dmΩ", CONFIG_MONITORING_LED_INTERVAL_MS,
                 CONFIG_MONITORING_LED_SAMPLES, CONFIG_MONITORING_LED_GAIN, CONFIG_MONITORING_LED_SHUNT_MILLIOHM);
    }
    else
    {
        ESP_LOGI(TAG, "LED current monitoring not supported on this target");
    }
#else
    ESP_LOGI(TAG, "LED current monitoring disabled by Kconfig");
#endif

#if CONFIG_MONITORING_BATTERY_ENABLE
    if (BatteryMonitor::isEnabled())
    {
        bm_.setup();
        ESP_LOGI(TAG, "Battery monitoring enabled. Interval=%dms, Samples=%d, R-Top=%dΩ, R-Bottom=%dΩ", CONFIG_MONITORING_BATTERY_INTERVAL_MS,
                 CONFIG_MONITORING_BATTERY_SAMPLES, CONFIG_MONITORING_BATTERY_DIVIDER_R_TOP_OHM, CONFIG_MONITORING_BATTERY_DIVIDER_R_BOTTOM_OHM);
    }
    else
    {
        ESP_LOGI(TAG, "Battery monitoring not supported on this target");
    }
#else
    ESP_LOGI(TAG, "Battery monitoring disabled by Kconfig");
#endif
}

void MonitoringManager::start()
{
    if (!isEnabled())
    {
        ESP_LOGI(TAG, "No monitoring features enabled, task not started");
        return;
    }

    if (task_ == nullptr)
    {
        xTaskCreate(&MonitoringManager::taskEntry, "MonitoringTask", 2048, this, 1, &task_);
        ESP_LOGI(TAG, "Monitoring task started");
    }
}

void MonitoringManager::stop()
{
    if (task_)
    {
        TaskHandle_t toDelete = task_;
        task_ = nullptr;
        vTaskDelete(toDelete);
        ESP_LOGI(TAG, "Monitoring task stopped");
    }
}

void MonitoringManager::taskEntry(void* arg)
{
    static_cast<MonitoringManager*>(arg)->run();
}

void MonitoringManager::run()
{
    if (!isEnabled())
    {
        vTaskDelete(nullptr);
        return;
    }

    TickType_t now_tick = xTaskGetTickCount();

#if CONFIG_MONITORING_LED_CURRENT
    TickType_t next_tick_led = now_tick;
    const TickType_t led_period = pdMS_TO_TICKS(CONFIG_MONITORING_LED_INTERVAL_MS);
#endif

#if CONFIG_MONITORING_BATTERY_ENABLE
    TickType_t next_tick_bat = now_tick;
    const TickType_t batt_period = pdMS_TO_TICKS(CONFIG_MONITORING_BATTERY_INTERVAL_MS);
#endif

    while (true)
    {
        now_tick = xTaskGetTickCount();
        TickType_t wait_ticks = pdMS_TO_TICKS(50);  // Default wait time

#if CONFIG_MONITORING_LED_CURRENT
        if (CurrentMonitor::isEnabled() && now_tick >= next_tick_led)
        {
            float ma = cm_.getCurrentMilliAmps();
            last_current_ma_.store(ma);
            next_tick_led = now_tick + led_period;
        }
        if (CurrentMonitor::isEnabled())
        {
            TickType_t to_led = (next_tick_led > now_tick) ? (next_tick_led - now_tick) : 1;
            if (to_led < wait_ticks)
            {
                wait_ticks = to_led;
            }
        }
#endif

#if CONFIG_MONITORING_BATTERY_ENABLE
        if (BatteryMonitor::isEnabled() && now_tick >= next_tick_bat)
        {
            const auto status = bm_.getBatteryStatus();
            if (status.valid)
            {
                std::lock_guard<std::mutex> lock(battery_mutex_);
                last_battery_status_ = status;
            }
            next_tick_bat = now_tick + batt_period;
        }
        if (BatteryMonitor::isEnabled())
        {
            TickType_t to_batt = (next_tick_bat > now_tick) ? (next_tick_bat - now_tick) : 1;
            if (to_batt < wait_ticks)
            {
                wait_ticks = to_batt;
            }
        }
#endif

        if (wait_ticks == 0)
        {
            wait_ticks = 1;
        }
        vTaskDelay(wait_ticks);
    }
}

float MonitoringManager::getCurrentMilliAmps() const
{
#if CONFIG_MONITORING_LED_CURRENT
    if (CurrentMonitor::isEnabled())
        return last_current_ma_.load();
#endif
    return 0.0f;
}

BatteryStatus MonitoringManager::getBatteryStatus() const
{
#if CONFIG_MONITORING_BATTERY_ENABLE
    if (BatteryMonitor::isEnabled())
    {
        std::lock_guard<std::mutex> lock(battery_mutex_);
        return last_battery_status_;
    }
#endif
    return {0, 0.0f, false};
}
