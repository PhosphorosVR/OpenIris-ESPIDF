// Only built with CONFIG_CAMERA_STATUS (see CMakeLists.txt).
#include "CameraManager.hpp"

static const char* CAMERA_STATUS_TAG = "[CAMERA_STATUS]";

const char* cameraRunStateName(const CameraRunState state)
{
    switch (state)
    {
        case CameraRunState::Starting:
            return "starting";
        case CameraRunState::Running:
            return "running";
        case CameraRunState::Failed:
            return "failed";
        default:
            return "uninitialized";
    }
}

const char* resetReasonName(const esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:
            return "task_watchdog";
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power_glitch";
        case ESP_RST_CPU_LOCKUP:
            return "cpu_lockup";
        default:
            return "unknown";
    }
}

// Power-on, restart_device and a host reset over USB. Anything else may be what an
// ESD test is looking for, so it gets a WARN line and ends up in the persistent log.
static bool isRoutineReset(const esp_reset_reason_t reason)
{
    return reason == ESP_RST_POWERON || reason == ESP_RST_SW || reason == ESP_RST_USB;
}

void CameraManager::beginSetup()
{
    if (!boot_seen)
    {
        boot_seen = true;
        status.reset_reason = esp_reset_reason();
        if (isRoutineReset(status.reset_reason))
        {
            ESP_LOGI(CAMERA_STATUS_TAG, "Last reset: %s", resetReasonName(status.reset_reason));
        }
        else
        {
            ESP_LOGW(CAMERA_STATUS_TAG, "Last reset: %s (not a routine restart)", resetReasonName(status.reset_reason));
        }
#if CONFIG_CAMERA_POWER_CONTROL
        // The camera is powered but not yet initialized: the only safe moment.
        camLines.probe();
#endif
    }
    status.state = CameraRunState::Starting;
}

void CameraManager::endSetup(const esp_err_t result)
{
    status.init_result = result;
    if (result != ESP_OK)
    {
        status.state = CameraRunState::Failed;
        return;
    }
    const sensor_t* sensor = esp_camera_sensor_get();
    status.pid = sensor ? sensor->id.PID : 0;
    status.state = CameraRunState::Running;
}

CameraStatus CameraManager::getStatus() const
{
    return status;
}
