// Only built with CONFIG_CAMERA_RECOVERY_ENABLE (see CMakeLists.txt).
// The frame gate: a recovery may only take the driver down while no consumer holds a
// frame (UVC sends it zero-copy) or waits inside esp_camera_fb_get().
#include "CameraGate.hpp"

#include "CameraManager.hpp"
#include "freertos/event_groups.h"

namespace
{
// A closed gate makes a consumer wait this long before it gets no frame, so the UVC video
// task logs a failed capture every few seconds instead of every 30 ms.
constexpr uint32_t kClosedWaitMs = 3000;
constexpr EventBits_t kOpenBit = BIT0;
constexpr uint32_t kDrainPollMs = 5;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
EventGroupHandle_t s_events = nullptr;
CameraManager* s_owner = nullptr;
bool s_open = false;
int s_users = 0;  // frames handed out plus fb_get calls in progress

bool enter()
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(kClosedWaitMs);
    while (true)
    {
        portENTER_CRITICAL(&s_lock);
        if (s_open)
        {
            ++s_users;
            portEXIT_CRITICAL(&s_lock);
            return true;
        }
        portEXIT_CRITICAL(&s_lock);

        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (!s_events || elapsed >= wait)
        {
            return false;
        }
        xEventGroupWaitBits(s_events, kOpenBit, pdFALSE, pdTRUE, wait - elapsed);
    }
}

void leave()
{
    portENTER_CRITICAL(&s_lock);
    // UVC can return the same frame from the stop and the transfer-complete path.
    if (s_users > 0)
    {
        --s_users;
    }
    portEXIT_CRITICAL(&s_lock);
}
}  // namespace

void cameraGateInit(CameraManager* owner)
{
    s_owner = owner;
    if (!s_events)
    {
        s_events = xEventGroupCreate();
    }
}

bool cameraGateClose(const uint32_t drain_timeout_ms)
{
    portENTER_CRITICAL(&s_lock);
    s_open = false;
    portEXIT_CRITICAL(&s_lock);
    if (s_events)
    {
        xEventGroupClearBits(s_events, kOpenBit);
    }

    const TickType_t start = xTaskGetTickCount();
    while (true)
    {
        portENTER_CRITICAL(&s_lock);
        const int users = s_users;
        portEXIT_CRITICAL(&s_lock);
        if (users == 0)
        {
            return true;
        }
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(drain_timeout_ms))
        {
            cameraGateOpen();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kDrainPollMs));
    }
}

void cameraGateOpen()
{
    portENTER_CRITICAL(&s_lock);
    s_open = true;
    portEXIT_CRITICAL(&s_lock);
    if (s_events)
    {
        xEventGroupSetBits(s_events, kOpenBit);
    }
}

camera_fb_t* cameraAcquireFrame()
{
    if (!enter())
    {
        if (s_owner)
        {
            s_owner->onFrameMissing();
        }
        return nullptr;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb)
    {
        leave();
        if (s_owner)
        {
            s_owner->onFrameMissing();
        }
    }
    return fb;
}

void cameraReleaseFrame(camera_fb_t* fb)
{
    esp_camera_fb_return(fb);
    leave();
}

uint16_t cameraSensorPid()
{
    // Cached at init: the driver's sensor struct disappears during a recovery.
    return s_owner ? s_owner->sensorPid() : 0;
}
