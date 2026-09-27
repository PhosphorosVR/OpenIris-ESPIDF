// Only built with CONFIG_CAMERA_POWER_CONTROL (see CMakeLists.txt).
// Sequence and timing: docs/CAM_CE_RESET_Analyse.md, sections 4, 5 and 7.
#include "CameraManager.hpp"

#include <algorithm>
#include "RailSense.hpp"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static const char* CAMERA_POWER_TAG = "[CAMERA_POWER]";

namespace
{
// OV3660 DS fig. 2-6
constexpr uint32_t kResetBeforeOffUs = 2000;  // RESET low before CE low (t6); node tau ~90 us
constexpr uint32_t kRailUpMs = 20;            // CE release to RESET release: rise < 5 ms + t2 + t3, about doubled
constexpr uint32_t kResetToSccbMs = 25;       // RESET release to the first SCCB access, t4 >= 20 ms
constexpr uint32_t kFirstFrameMs = 2000;      // success needs a frame within this time

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

constexpr int kDvpPins[] = {
    CONFIG_Y2_GPIO_NUM, CONFIG_Y3_GPIO_NUM, CONFIG_Y4_GPIO_NUM,    CONFIG_Y5_GPIO_NUM,   CONFIG_Y6_GPIO_NUM,   CONFIG_Y7_GPIO_NUM,
    CONFIG_Y8_GPIO_NUM, CONFIG_Y9_GPIO_NUM, CONFIG_VSYNC_GPIO_NUM, CONFIG_HREF_GPIO_NUM, CONFIG_PCLK_GPIO_NUM,
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
}  // namespace

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

// Camera off: nothing may feed the dead rail. esp_camera_init() configures every one
// of these pins again, so parking never needs undoing.
void CameraManager::parkPins()
{
    // XCLK has no path into DOVDD, 3.3 V on it exceeds VDD-IO + 1 V (DS tab. 8-1).
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

namespace
{
// The reinit runs the whole driver init, which needs about 3.5 KB at boot (main task).
// Called from the serial task that left only ~440 bytes (measured), so the cycle runs
// on its own short-lived task and the caller waits for it.
constexpr uint32_t kPowerCycleStackBytes = 6144;

struct PowerCycleJob
{
    CameraManager* manager;
    const PowerCycleRequest* request;
    PowerCycleReport* report;
    SemaphoreHandle_t done;
};
}  // namespace

void CameraManager::powerCycleTask(void* arg)
{
    auto* job = static_cast<PowerCycleJob*>(arg);
    *job->report = job->manager->runPowerCycle(*job->request);
    job->report->stack_free_min = uxTaskGetStackHighWaterMark(nullptr);
    xSemaphoreGive(job->done);
    vTaskDelete(nullptr);
}

PowerCycleReport CameraManager::powerCycle(const PowerCycleRequest& request)
{
    PowerCycleReport report{};
    PowerCycleJob job{this, &request, &report, xSemaphoreCreateBinary()};
    if (!job.done || xTaskCreate(powerCycleTask, "cam_power", kPowerCycleStackBytes, &job, uxTaskPriorityGet(nullptr), nullptr) != pdPASS)
    {
        // Nothing touched, the camera keeps running.
        ESP_LOGE(CAMERA_POWER_TAG, "Power cycle task could not be created");
        report.pid_before = report.pid_after = status.pid;
        report.failed_step = "no_task";
    }
    else
    {
        // No timeout: every step is bounded, and the task writes into this frame.
        xSemaphoreTake(job.done, portMAX_DELAY);
    }
    if (job.done)
    {
        vSemaphoreDelete(job.done);
    }
    return report;
}

PowerCycleReport CameraManager::runPowerCycle(const PowerCycleRequest& request)
{
    PowerCycleReport report{};
    const int64_t start_us = esp_timer_get_time();
    report.pid_before = status.pid;
    ESP_LOGW(CAMERA_POWER_TAG, "Power cycle: off %lu ms%s%s", static_cast<unsigned long>(request.off_ms), request.trace ? ", trace" : "",
             request.force ? ", forced" : "");

    // 1. Driver down. No consumer runs (the caller checks), so no frame is in flight.
    status.state = CameraRunState::Stopping;
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    esp_camera_deinit();
    camera_sensor = nullptr;
    xSemaphoreGive(sensor_mutex);
    status.state = CameraRunState::Off;

    // 2. Park
    this->parkPins();

    // 3. RESET low before CE low: the reset node's pull-up goes to 3V3 and would feed
    // the dead rail through the RESETB pad (~260 uA) unless held low.
    report.reset = camLines.holdReset(request.force);
    esp_rom_delay_us(kResetBeforeOffUs);

    // 4. Positive control: camera powered and in reset, outputs high impedance.
    RailSense rail;
    const bool sensing = rail.begin();
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
            report.fall.push_back({static_cast<uint32_t>(esp_timer_get_time() - off_start_us), {static_cast<int16_t>(mv[0]), static_cast<int16_t>(mv[1])}});
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
            const auto t_us = static_cast<uint32_t>(esp_timer_get_time() - on_start_us);
            report.rise.push_back({t_us, {static_cast<int16_t>(mv[0]), static_cast<int16_t>(mv[1])}});
            if (report.rise_us < 0 && nearControl(mv, report.control_mv))
            {
                report.rise_us = static_cast<int32_t>(t_us);
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

    // 9./10. RESET released, then t4 before the driver talks SCCB
    if (camLines.releaseReset(request.force) == LineOutcome::Error)
    {
        report.reset = LineOutcome::Error;
    }
    sleepAtLeastMs(kResetToSccbMs);

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

    // 13. First frame over DVP, no SCCB involved
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

    report.duration_ms = msSince(start_us);
    ESP_LOGW(CAMERA_POWER_TAG, "Power cycle %s: rail %s (control %d/%d mV, end %d/%d mV), off %lu ms, reinit %s, first frame %s, %lu ms",
             report.failed_step ? report.failed_step : "ok", railVerdictName(report.rail), report.control_mv[0], report.control_mv[1],
             report.end_mv[0], report.end_mv[1], static_cast<unsigned long>(report.off_ms), esp_err_to_name(report.reinit),
             report.first_frame ? "yes" : "no", static_cast<unsigned long>(report.duration_ms));
    return report;
}
