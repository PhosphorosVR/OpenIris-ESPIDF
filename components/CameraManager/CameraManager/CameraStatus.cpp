// Only built with CONFIG_CAMERA_STATUS (see CMakeLists.txt).
#include "CameraManager.hpp"
#if CONFIG_CAMERA_RECOVERY_ENABLE
#include "CameraGate.hpp"
#endif

static const char* CAMERA_STATUS_TAG = "[CAMERA_STATUS]";

const char* cameraRunStateName(const CameraRunState state)
{
    switch (state)
    {
        case CameraRunState::Starting:
            return "starting";
        case CameraRunState::Running:
            return "running";
        case CameraRunState::Stopping:
            return "stopping";
        case CameraRunState::Off:
            return "off";
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

const char* linePresenceName(const LinePresence presence)
{
    switch (presence)
    {
        case LinePresence::Present:
            return "present";
        case LinePresence::Absent:
            return "absent";
        default:
            return "unknown";
    }
}

const char* lineOutcomeName(const LineOutcome outcome)
{
    switch (outcome)
    {
        case LineOutcome::Done:
            return "done";
        case LineOutcome::NotAvailable:
            return "not_available";
        default:
            return "error";
    }
}

const char* railVerdictName(const RailVerdict verdict)
{
    switch (verdict)
    {
        case RailVerdict::Collapsed:
            return "collapsed";
        case RailVerdict::Partial:
            return "partial";
        case RailVerdict::NotCollapsed:
            return "not_collapsed";
        case RailVerdict::Inconclusive:
            return "inconclusive";
        default:
            return "not_checked";
    }
}

const char* recoveryLevelName(const RecoveryLevel level)
{
    switch (level)
    {
        case RecoveryLevel::HwReset:
            return "reset";
        case RecoveryLevel::PowerCycle:
            return "power_cycle";
        default:
            return "reinit";
    }
}

const char* recoveryTriggerName(const RecoveryTrigger trigger)
{
    switch (trigger)
    {
        case RecoveryTrigger::Command:
            return "command";
        case RecoveryTrigger::FrameTimeout:
            return "frame_timeout";
        case RecoveryTrigger::BootFailure:
            return "boot_failure";
        default:
            return "bench";
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
        this->startCameraTask();
#if CONFIG_CAMERA_RECOVERY_ENABLE
        this->checkRestartMarker();
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
#if CONFIG_CAMERA_RECOVERY_ENABLE
    // Within a cycle the gate opens only after the first frame proved the camera.
    if (!in_cycle)
    {
        this->watchBootRun();
        cameraGateOpen();
    }
#endif
}

CameraStatus CameraManager::getStatus() const
{
    CameraStatus copy = status;
#if CONFIG_CAMERA_RECOVERY_ENABLE
    copy.frames_missing = frames_missing.load(std::memory_order_relaxed);
#endif
    return copy;
}
