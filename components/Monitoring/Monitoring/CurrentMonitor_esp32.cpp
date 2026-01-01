#include "CurrentMonitor.hpp"
#include <esp_log.h>

static const char *TAG_CM = "[CurrentMonitor]";

void CurrentMonitor::setup()
{
    ESP_LOGI(TAG_CM, "LED current monitoring disabled");
}

float CurrentMonitor::getCurrentMilliAmps() const
{
    return 0.0f;
}
