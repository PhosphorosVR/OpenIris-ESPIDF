#include "CurrentMonitor.hpp"
#include <esp_log.h>

static const char *TAG_CM = "[CurrentMonitor]";

CurrentMonitor::CurrentMonitor()
{
    // empty as esp32 doesn't support this
    // but without a separate implementation, the linker will complain :c
}

void CurrentMonitor::setup()
{
    ESP_LOGI(TAG_CM, "LED current monitoring disabled");
}

float CurrentMonitor::getCurrentMilliAmps() const
{
    return 0.0f;
}

float CurrentMonitor::pollAndGetMilliAmps()
{
    sampleOnce();
    return getCurrentMilliAmps();
}

void CurrentMonitor::sampleOnce()
{
    (void)0;
}

#ifdef CONFIG_MONITORING_LED_CURRENT
void CurrentMonitor::init_adc()
{
}

int CurrentMonitor::read_mv_once()
{
    return 0;
}
#endif