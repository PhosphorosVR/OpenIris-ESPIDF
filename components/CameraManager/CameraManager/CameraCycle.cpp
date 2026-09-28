// Only built with CONFIG_CAMERA_STATUS (see CMakeLists.txt).
// Camera restart in three levels. Sequence and timing: docs/CAM_CE_RESET_Analyse.md,
// sections 4, 5 and 7.
#include "CameraManager.hpp"

#include <algorithm>
#include "RailSense.hpp"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#if CONFIG_CAMERA_RECOVERY_ENABLE
#include "CameraGate.hpp"
#endif

static const char* CAMERA_CYCLE_TAG = "[CAMERA_CYCLE]";

namespace
{
// OV3660 DS fig. 2-6
constexpr uint32_t kResetBeforeOffUs = 2000;  // RESET low before CE low (t6), and the plain reset pulse; node tau ~90 us
constexpr uint32_t kRailUpMs = 20;            // CE release to RESET release: rise < 5 ms + t2 + t3, about doubled
constexpr uint32_t kResetToSccbMs = 25;       // RESET release to the first SCCB access, t4 >= 20 ms
constexpr uint32_t kFirstFrameMs = 2000;      // success needs a frame within this time

// Adaptive off time: CE stays low until the rail check says collapsed (Rev.5: < 20 ms).
constexpr uint32_t kAdaptiveStepMs = 100;
constexpr uint32_t kAdaptiveMaxOffMs = 3000;

// Traces: the fall is slow (LDO discharge or leakage), the rise is fast.
constexpr uint32_t kFallStepMs = 20;
constexpr size_t kFallMaxPoints = 150;  // 3 s
constexpr uint32_t kRiseStepUs = 500;

// Rail classification. Measured on Rev.5 (2 x 50 cycles): control 3102-3300 mV, end
// 410-451 mV; Rev.4.5 without lines: end within ~10 mV of control. Wide margins kept.
constexpr int kControlMinMv = 2600;
constexpr int kCollapsedMaxMv = 1000;
constexpr int kNotCollapsedMarginMv = 300;
constexpr int kControlSamples = 4;

// The reinit runs the whole driver init: ~3.5 KB at boot (main task), a full cycle took
// at most 3.4 KB here. Command tasks have too little left (440 bytes measured).
constexpr uint32_t kCameraTaskStackBytes = 6144;
constexpr UBaseType_t kCameraTaskPriority = 2;
// A consumer can sit in esp_camera_fb_get() for up to 8 s on the S3.
constexpr uint32_t kDrainTimeoutMs = 9000;

constexpr int kDvpPins[] = {
    CONFIG_Y2_GPIO_NUM, CONFIG_Y3_GPIO_NUM, CONFIG_Y4_GPIO_NUM,    CONFIG_Y5_GPIO_NUM,   CONFIG_Y6_GPIO_NUM,   CONFIG_Y7_GPIO_NUM,
    CONFIG_Y8_GPIO_NUM, CONFIG_Y9_GPIO_NUM, CONFIG_VSYNC_GPIO_NUM, CONFIG_HREF_GPIO_NUM, CONFIG_PCLK_GPIO_NUM,
};

struct CycleJob
{
    CycleRequest request;
    RecoveryTrigger trigger;
    CycleReport* report;  // nullptr: nobody waits
    SemaphoreHandle_t done;
};

uint64_t pinBit(int pin)
{
    return pin >= 0 ? 1ULL << pin : 0;
}

uint32_t msSince(int64_t t0_us)
{
    return static_cast<uint32_t>((esp_timer_get_time() - t0_us) / 1000);
}

// vTaskDelay(n) may return after n-1 ticks; one more guarantees the minimum.
void sleepAtLeastMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

RailVerdict classifyPad(int control_mv, int end_mv)
{
    if (control_mv < kControlMinMv || end_mv < 0)
    {
        return RailVerdict::Inconclusive;
    }
    if (end_mv <= kCollapsedMaxMv)
    {
        return RailVerdict::Collapsed;
    }
    if (end_mv >= control_mv - kNotCollapsedMarginMv)
    {
        return RailVerdict::NotCollapsed;
    }
    return RailVerdict::Partial;
}

// Both pads have to say the same, otherwise the method does not apply to this board.
RailVerdict classifyRail(const int (&control_mv)[2], const int (&end_mv)[2])
{
    const RailVerdict a = classifyPad(control_mv[0], end_mv[0]);
    const RailVerdict b = classifyPad(control_mv[1], end_mv[1]);
    return a == b ? a : RailVerdict::Inconclusive;
}

bool nearControl(const int (&mv)[2], const int (&control_mv)[2])
{
    return control_mv[0] >= kControlMinMv && control_mv[1] >= kControlMinMv && mv[0] >= control_mv[0] - kNotCollapsedMarginMv &&
           mv[1] >= control_mv[1] - kNotCollapsedMarginMv;
}

RailPoint point(int64_t since_us, const int (&mv)[2])
{
    return {static_cast<uint32_t>(esp_timer_get_time() - since_us), {static_cast<int16_t>(mv[0]), static_cast<int16_t>(mv[1])}};
}
}  // namespace

// Camera off: nothing may feed the dead rail. esp_camera_init() configures every one
// of these pins again, so parking never needs undoing. No test notices a missing park:
// the rail check cannot see XCLK and misses small feeds (analysis doc, section 5).
void CameraManager::parkPins()
{
    // XCLK has no path into DOVDD, 3.3 V on it exceeds VDD-IO + 1 V (DS tab. 8-1). On the
    // S3 the deinit leaves it routed to the camera clock; gpio_config() releases it.
    // DVP pads are camera outputs; pulled high, the output PMOS would feed the rail.
    uint64_t pulldown = pinBit(CONFIG_XCLK_GPIO_NUM);
    for (const int pin : kDvpPins)
    {
        pulldown |= pinBit(pin);
    }
    gpio_config_t park = {
        .pin_bit_mask = pulldown,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&park);

    // SCCB: the I2C driver leaves its internal pull-ups on, and through the 4.7 k board
    // pull-ups they would feed 2V8_Cx.
    gpio_config_t sccb = {
        .pin_bit_mask = pinBit(CONFIG_SIOD_GPIO_NUM) | pinBit(CONFIG_SIOC_GPIO_NUM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sccb);
}

void CameraManager::takeDriverDown()
{
    // Other tasks (setters, applyFrameSize, test hook) check camera_sensor under this
    // lock and after taking it; a check before taking it can see a stale pointer.
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    esp_camera_deinit();
    camera_sensor = nullptr;
    xSemaphoreGive(sensor_mutex);
}

void CameraManager::startCameraTask()
{
    if (cycle_queue)
    {
        return;
    }
#if CONFIG_CAMERA_RECOVERY_ENABLE
    cameraGateInit(this);
#endif
    cycle_queue = xQueueCreate(1, sizeof(CycleJob));
    if (!cycle_queue || xTaskCreate(cameraTask, "camera", kCameraTaskStackBytes, this, kCameraTaskPriority, nullptr) != pdPASS)
    {
        ESP_LOGE(CAMERA_CYCLE_TAG, "Camera task could not be created, camera restarts are unavailable");
        if (cycle_queue)
        {
            vQueueDelete(cycle_queue);
            cycle_queue = nullptr;
        }
    }
}

void CameraManager::cameraTask(void* arg)
{
    auto* self = static_cast<CameraManager*>(arg);
    CycleJob job{};
    while (true)
    {
        if (xQueueReceive(self->cycle_queue, &job, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        self->cycle_busy = true;
        CycleReport report{};
#if CONFIG_CAMERA_RECOVERY_ENABLE
        // Budget first: a refused request touches nothing.
        if (const char* refusal = self->admit(job.trigger))
        {
            report.level = job.request.level;
            report.pid_before = report.pid_after = self->status.pid;
            report.failed_step = refusal;
        }
        else
#endif
        {
            report = self->runCycle(job.request);
            report.stack_free_min = uxTaskGetStackHighWaterMark(nullptr);
#if CONFIG_CAMERA_RECOVERY_ENABLE
            self->recordCycle(job.trigger, report);
#endif
        }
        self->cycle_busy = false;
        if (job.report)
        {
            *job.report = std::move(report);
            xSemaphoreGive(job.done);
        }
    }
}

bool CameraManager::runCycleBlocking(const CycleRequest& request, const RecoveryTrigger trigger, CycleReport& report)
{
    if (!cycle_queue || cycle_busy)
    {
        return false;
    }
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done)
    {
        return false;
    }
    const CycleJob job{request, trigger, &report, done};
    if (xQueueSend(cycle_queue, &job, 0) != pdTRUE)
    {
        vSemaphoreDelete(done);
        return false;
    }
    // No timeout: every step is bounded, and the camera task writes into the caller's report.
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return true;
}

#if CONFIG_CAMERA_RECOVERY_ENABLE
bool CameraManager::submitCycle(const CycleRequest& request, const RecoveryTrigger trigger)
{
    // Nobody waits; a full queue means a restart is already pending.
    const CycleJob job{request, trigger, nullptr, nullptr};
    return cycle_queue && xQueueSend(cycle_queue, &job, 0) == pdTRUE;
}

int CameraManager::applyFrameSize(const framesize_t frameSize)
{
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    // Remembered so a restart can restore what the consumer asked for.
    requested_framesize = frameSize;
    int ret = -1;
    // Checked under the lock: a restart clears it while the driver is down.
    if (camera_sensor && camera_sensor->pixformat == PIXFORMAT_JPEG)
    {
        ret = camera_sensor->set_framesize(camera_sensor, frameSize);
    }
    xSemaphoreGive(sensor_mutex);
    return ret;
}
#endif

CycleReport CameraManager::runCycle(const CycleRequest& request)
{
    CycleReport report{};
    report.level = request.level;
    report.pid_before = status.pid;
    const int64_t start_us = esp_timer_get_time();
    const bool uses_lines = request.level != RecoveryLevel::Reinit;
    [[maybe_unused]] const bool power = request.level == RecoveryLevel::PowerCycle;

    // A level the board does not have is refused before anything is touched.
#if CONFIG_CAMERA_POWER_CONTROL
    const bool supported = !uses_lines || camLines.available() || request.force;
#else
    const bool supported = !uses_lines;
#endif
    if (!supported)
    {
        report.failed_step = "not_supported";
        return report;
    }

    ESP_LOGW(CAMERA_CYCLE_TAG, "Camera %s%s%s", recoveryLevelName(request.level), request.trace ? ", trace" : "", request.force ? ", forced" : "");

#if CONFIG_CAMERA_RECOVERY_ENABLE
    // 0. No consumer may hold a frame or wait inside the driver while it goes down.
    if (!cameraGateClose(kDrainTimeoutMs))
    {
        report.failed_step = "drain_timeout";
        report.duration_ms = msSince(start_us);
        ESP_LOGE(CAMERA_CYCLE_TAG, "Camera %s refused: consumers did not return their frames", recoveryLevelName(request.level));
        return report;
    }
#endif
    in_cycle = true;

    // 1. Driver down, 2. park
    status.state = CameraRunState::Stopping;
    this->takeDriverDown();
    status.state = CameraRunState::Off;
    this->parkPins();

    RailSense rail;
    bool sensing = false;
#if CONFIG_CAMERA_POWER_CONTROL
    if (uses_lines)
    {
        // 3. RESET low first: the reset node's pull-up goes to 3V3 and would feed the
        // dead rail through the RESETB pad (~260 uA) unless held low.
        report.reset = camLines.holdReset(request.force);
        esp_rom_delay_us(kResetBeforeOffUs);
    }

    if (power)
    {
        // 4. Positive control: camera powered and in reset, outputs high impedance.
        sensing = rail.begin();
        if (sensing)
        {
            rail.measure(report.control_mv, kControlSamples);
        }

        // 5./6. CE low for the off time
        report.power = camLines.powerOff(request.force);
        const int64_t off_start_us = esp_timer_get_time();
        if (sensing && request.trace)
        {
            report.fall.reserve(std::min<size_t>(kFallMaxPoints, request.off_ms / kFallStepMs + 1));
            TickType_t wake = xTaskGetTickCount();
            while (report.fall.size() < kFallMaxPoints && msSince(off_start_us) + kFallStepMs <= request.off_ms)
            {
                int mv[2];
                rail.measure(mv, 1);
                report.fall.push_back(point(off_start_us, mv));
                vTaskDelayUntil(&wake, pdMS_TO_TICKS(kFallStepMs));
            }
        }
        if (const uint32_t elapsed = msSince(off_start_us); elapsed < request.off_ms)
        {
            sleepAtLeastMs(request.off_ms - elapsed);
        }
        if (sensing)
        {
            rail.measure(report.end_mv, kControlSamples);
            if (request.adaptive_off)
            {
                RailVerdict verdict = classifyRail(report.control_mv, report.end_mv);
                while ((verdict == RailVerdict::Partial || verdict == RailVerdict::NotCollapsed) && msSince(off_start_us) < kAdaptiveMaxOffMs)
                {
                    sleepAtLeastMs(kAdaptiveStepMs);
                    rail.measure(report.end_mv, kControlSamples);
                    verdict = classifyRail(report.control_mv, report.end_mv);
                }
            }
        }

        // 7. CE released, RESET still held while the rails come up
        const LineOutcome power_on = camLines.powerOn(request.force);
        const int64_t on_start_us = esp_timer_get_time();
        report.off_ms = static_cast<uint32_t>((on_start_us - off_start_us) / 1000);
        if (power_on == LineOutcome::Error)
        {
            report.power = LineOutcome::Error;
        }

        // 8. Rails up. With a trace the pads are sampled as fast as the step allows.
        if (sensing && request.trace)
        {
            int64_t next_us = on_start_us;
            while (esp_timer_get_time() - on_start_us < kRailUpMs * 1000)
            {
                int mv[2];
                rail.measure(mv, 1);
                const RailPoint p = point(on_start_us, mv);
                report.rise.push_back(p);
                if (report.rise_us < 0 && nearControl(mv, report.control_mv))
                {
                    report.rise_us = static_cast<int32_t>(p.t_us);
                }
                next_us += kRiseStepUs;
                if (const int64_t wait_us = next_us - esp_timer_get_time(); wait_us > 0)
                {
                    esp_rom_delay_us(static_cast<uint32_t>(wait_us));
                }
            }
        }
        if (const uint32_t up = msSince(on_start_us); up < kRailUpMs)
        {
            sleepAtLeastMs(kRailUpMs - up);
        }
    }

    if (uses_lines)
    {
        // 9./10. RESET released, then t4 before the driver talks SCCB
        if (camLines.releaseReset(request.force) == LineOutcome::Error)
        {
            report.reset = LineOutcome::Error;
        }
        sleepAtLeastMs(kResetToSccbMs);
    }
#endif

    // 11. Sense pads back to digital, otherwise their DVP inputs stay dead
    rail.end();
    if (sensing)
    {
        report.rail = classifyRail(report.control_mv, report.end_mv);
    }

    // 12. Same path as at boot, including the XCLK switch
    const bool camera_up = this->setupCamera();
    report.reinit = status.init_result;
    report.pid_after = status.pid;
#if CONFIG_CAMERA_RECOVERY_ENABLE
    if (camera_up && requested_framesize != FRAMESIZE_INVALID)
    {
        this->applyFrameSize(requested_framesize);
    }
#endif

    // 13. First frame over DVP, no SCCB involved. The gate is still closed, so no
    // consumer competes for it.
    if (!camera_up)
    {
        report.failed_step = "reinit";
    }
    else
    {
        const int64_t fb_start_us = esp_timer_get_time();
        camera_fb_t* fb = esp_camera_fb_get();
        report.first_frame_ms = msSince(fb_start_us);
        if (fb)
        {
            esp_camera_fb_return(fb);
        }
        report.first_frame = fb && report.first_frame_ms <= kFirstFrameMs;
        if (!report.first_frame)
        {
            report.failed_step = "first_frame";
        }
        else if (report.pid_before != 0 && report.pid_after != report.pid_before)
        {
            report.failed_step = "pid_changed";
        }
    }
    if (report.power == LineOutcome::Error || report.reset == LineOutcome::Error)
    {
        report.failed_step = report.power == LineOutcome::Error ? "power_line" : "reset_line";
    }

    if (report.failed_step)
    {
        // End state after a failure: powered, lines released, driver down (F6).
        this->takeDriverDown();
        status.state = CameraRunState::Failed;
    }
#if CONFIG_CAMERA_RECOVERY_ENABLE
    else
    {
        cameraGateOpen();
    }
#endif
    in_cycle = false;

    report.duration_ms = msSince(start_us);
    ESP_LOGW(CAMERA_CYCLE_TAG, "Camera %s %s: rail %s (control %d/%d mV, end %d/%d mV), off %lu ms, reinit %s, first frame %s, %lu ms",
             recoveryLevelName(request.level), report.failed_step ? report.failed_step : "ok", railVerdictName(report.rail), report.control_mv[0],
             report.control_mv[1], report.end_mv[0], report.end_mv[1], static_cast<unsigned long>(report.off_ms), esp_err_to_name(report.reinit),
             report.first_frame ? "yes" : "no", static_cast<unsigned long>(report.duration_ms));
    return report;
}
