#include "MonitoringManager.hpp"
#include <esp_log.h>

static const char *TAG_MM = "[MonitoringManager]";

void MonitoringManager::setup()
{
    ESP_LOGI(TAG_MM, "Monitoring disabled by Kconfig");
}

void MonitoringManager::start()
{
}

void MonitoringManager::stop()
{
}

void MonitoringManager::taskEntry(void *arg)
{
}

void MonitoringManager::run()
{
}

float MonitoringManager::getCurrentMilliAmps() const {
    return 0.0f;
}

float MonitoringManager::getBatteryVoltageMilliVolts() const
{
    return 0.0f;
}