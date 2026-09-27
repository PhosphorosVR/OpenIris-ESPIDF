// Only built with CONFIG_CAMERA_RECOVERY_ENABLE (see CMakeLists.txt).
// The frame gate: a recovery may only take the driver down while no consumer holds a
// frame (UVC sends it zero-copy) or waits inside esp_camera_fb_get().
#include "CameraGate.hpp"

#include <atomic>
#include "CameraManager.hpp"
#include "freertos/event_groups.h"

namespace
{
// A closed gate makes a consumer wait this long before it gets no frame, so the UVC video
// task logs a failed capture every few seconds instead of every 30 ms.
constexpr uint32_t kClosedWaitMs = 3000;
constexpr EventBits_t kOpenBit = BIT0;
constexpr uint32_t kDrainPollMs = 5;
// A frame out for this long is not being sent any more (a transfer takes ~30 ms): the
// host stopped reading. Windows does that when an app closes the camera; the transfer
// stays pending until the host reads again.
constexpr uint32_t kStaleMs = 500;
// The driver has fb_count (2) buffers; room to spare.
constexpr int kMaxOut = 4;

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
EventGroupHandle_t s_events = nullptr;
CameraManager* s_owner = nullptr;
CameraFrameReclaim s_reclaim = nullptr;
bool s_open = false;
int s_in_driver = 0;                // consumers inside esp_camera_fb_get()
camera_fb_t* s_out[kMaxOut] = {};  // frames handed out and not returned
TickType_t s_out_since[kMaxOut] = {};
ReclaimResult s_last_reclaim = ReclaimResult::None;
uint32_t s_drain_timeouts = 0;
uint32_t s_taken_back = 0;
int s_timeout_in_driver = 0;
int s_timeout_frames_out = 0;

// Under s_lock.
int framesOutLocked()
{
    int out = 0;
    for (const auto* fb : s_out)
    {
        out += fb != nullptr;
    }
    return out;
}
std::atomic<uint32_t> s_missed_in_row{0};

// Under s_lock.
int usersLocked()
{
    return s_in_driver + framesOutLocked();
}

bool enter()
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(kClosedWaitMs);
    while (true)
    {
        portENTER_CRITICAL(&s_lock);
        if (s_open)
        {
            ++s_in_driver;
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

// Leaves the driver; a delivered frame is recorded as out.
void leave(camera_fb_t* fb)
{
    portENTER_CRITICAL(&s_lock);
    if (s_in_driver > 0)
    {
        --s_in_driver;
    }
    if (fb)
    {
        for (int i = 0; i < kMaxOut; ++i)
        {
            if (!s_out[i])
            {
                s_out[i] = fb;
                s_out_since[i] = xTaskGetTickCount();
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

// Under s_lock: every frame out has been out for at least kStaleMs.
bool allOutStaleLocked(const TickType_t now)
{
    bool any = false;
    for (int i = 0; i < kMaxOut; ++i)
    {
        if (s_out[i])
        {
            any = true;
            if (now - s_out_since[i] < pdMS_TO_TICKS(kStaleMs))
            {
                return false;
            }
        }
    }
    return any;
}

void notifyMissing()
{
    const uint32_t in_row = s_missed_in_row.fetch_add(1, std::memory_order_relaxed) + 1;
    if (s_owner)
    {
        s_owner->onFrameMissing(in_row);
    }
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

void cameraGateSetReclaim(const CameraFrameReclaim reclaim)
{
    s_reclaim = reclaim;
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
    bool reclaimed = false;
    while (true)
    {
        const TickType_t now = xTaskGetTickCount();
        portENTER_CRITICAL(&s_lock);
        const int users = usersLocked();
        const bool stale = s_in_driver == 0 && allOutStaleLocked(now);
        portEXIT_CRITICAL(&s_lock);
        if (users == 0)
        {
            return true;
        }
        if (stale)
        {
            // First let the consumer give it back properly (UVC: only when not streaming).
            if (!reclaimed && s_reclaim)
            {
                reclaimed = true;
                s_last_reclaim = s_reclaim();
                continue;
            }
            // Still out: take it back. The driver is torn down next and frees the buffer
            // anyway; a late return from the consumer is not tracked any more and ignored.
            portENTER_CRITICAL(&s_lock);
            for (auto*& slot : s_out)
            {
                if (slot)
                {
                    slot = nullptr;
                    ++s_taken_back;
                }
            }
            portEXIT_CRITICAL(&s_lock);
            return true;
        }
        const TickType_t elapsed = now - start;
        if (elapsed >= pdMS_TO_TICKS(drain_timeout_ms))
        {
            portENTER_CRITICAL(&s_lock);
            ++s_drain_timeouts;
            s_timeout_in_driver = s_in_driver;
            s_timeout_frames_out = framesOutLocked();
            portEXIT_CRITICAL(&s_lock);
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
        notifyMissing();
        return nullptr;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    leave(fb);
    if (!fb)
    {
        notifyMissing();
        return nullptr;
    }
    s_missed_in_row.store(0, std::memory_order_relaxed);
    return fb;
}

void cameraReleaseFrame(camera_fb_t* fb)
{
    bool out = false;
    portENTER_CRITICAL(&s_lock);
    for (auto*& slot : s_out)
    {
        if (fb && slot == fb)
        {
            slot = nullptr;
            out = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    // A second return of the same frame (UVC's stop and transfer-complete paths can
    // race) must not reach the driver twice.
    if (out)
    {
        esp_camera_fb_return(fb);
    }
}

CameraGateState cameraGateState()
{
    CameraGateState state{};
    portENTER_CRITICAL(&s_lock);
    state.open = s_open;
    state.in_driver = s_in_driver;
    state.frames_out = framesOutLocked();
    state.last_reclaim = s_last_reclaim;
    state.drain_timeouts = s_drain_timeouts;
    state.taken_back = s_taken_back;
    state.timeout_in_driver = s_timeout_in_driver;
    state.timeout_frames_out = s_timeout_frames_out;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

uint16_t cameraSensorPid()
{
    // Cached at init: the driver's sensor struct disappears during a recovery.
    return s_owner ? s_owner->sensorPid() : 0;
}
