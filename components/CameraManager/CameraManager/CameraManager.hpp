#pragma once
#ifndef CAMERAMANAGER_HPP
#define CAMERAMANAGER_HPP

#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ProjectConfig.hpp>
#include <StateManager.hpp>

#define OV5640_XCLK_FREQ_HZ CONFIG_CAMERA_WIFI_XCLK_FREQ

#if CONFIG_CAMERA_STATUS
#include "esp_system.h"
#if CONFIG_CAMERA_POWER_CONTROL
#include "CamLines.hpp"
#endif

// Camera lifecycle as reported by get_camera_status (docs/CAM_CE_RESET_Analyse.md, section 10).
enum class CameraRunState : uint8_t
{
    Uninitialized,
    Starting,
    Running,
    Stopping,
    Off,
    Failed,
};

struct CameraStatus
{
    CameraRunState state = CameraRunState::Uninitialized;
    esp_err_t init_result = ESP_OK;  // last esp_camera_init()
    uint16_t pid = 0;                // 0 until a sensor was detected
    esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
};

const char* cameraRunStateName(CameraRunState state);
const char* resetReasonName(esp_reset_reason_t reason);

#if CONFIG_CAMERA_POWER_CONTROL
#include <vector>

// Result of the rail check (docs/CAM_CE_RESET_Analyse.md, section 5).
enum class RailVerdict : uint8_t
{
    NotChecked,
    Collapsed,     // rail below the end reading at both pads
    Partial,       // fell, but not below the collapse threshold
    NotCollapsed,  // did not fall (expected where the lines are absent)
    Inconclusive,  // no valid control reading, or the pads disagree
};

struct PowerCycleRequest
{
    uint32_t off_ms = 500;  // time CE is held low
    bool trace = false;     // record the fall and rise of the rail
    bool force = false;     // full sequence even where the probe found no lines
};

struct RailPoint
{
    uint32_t t_us;  // since CE low (fall) or CE release (rise)
    int16_t mv[2];
};

struct PowerCycleReport
{
    LineOutcome power = LineOutcome::NotAvailable;
    LineOutcome reset = LineOutcome::NotAvailable;
    RailVerdict rail = RailVerdict::NotChecked;
    int control_mv[2] = {-1, -1};  // powered and in reset, before CE low
    int end_mv[2] = {-1, -1};      // end of the off time
    uint32_t off_ms = 0;           // measured CE low time
    int32_t rise_us = -1;          // CE release until both pads are back near control; -1 if not traced
    esp_err_t reinit = ESP_FAIL;
    uint16_t pid_before = 0;
    uint16_t pid_after = 0;
    bool first_frame = false;
    uint32_t first_frame_ms = 0;
    uint32_t duration_ms = 0;
    const char* failed_step = nullptr;  // nullptr on success
    std::vector<RailPoint> fall;
    std::vector<RailPoint> rise;
};

const char* railVerdictName(RailVerdict verdict);
#endif
#endif

class CameraManager
{
   private:
    sensor_t* camera_sensor = nullptr;
    SemaphoreHandle_t sensor_mutex;
    std::shared_ptr<ProjectConfig> projectConfig;
    QueueHandle_t eventQueue;
    camera_config_t config;

   public:
    CameraManager(std::shared_ptr<ProjectConfig> projectConfig, QueueHandle_t eventQueue);
    int setCameraResolution(framesize_t frameSize);
    bool setupCamera();
    int setVFlip(int direction);
    int setHFlip(int direction);
    int setVieWindow(int offsetX, int offsetY, int outputX, int outputY);

   private:
    void loadConfigData();
    void setupCameraPinout();
    void setupCameraSensor();

#if CONFIG_CAMERA_STATUS
   public:
    CameraStatus getStatus() const;
#if CONFIG_CAMERA_POWER_CONTROL
    const CamLines& lines() const
    {
        return camLines;
    }
    // CameraPower.cpp. Synchronous; only while no consumer takes frames (the caller checks).
    PowerCycleReport powerCycle(const PowerCycleRequest& request);
#endif

   private:
    // CameraStatus.cpp
    void beginSetup();
    void endSetup(esp_err_t result);
#if CONFIG_CAMERA_POWER_CONTROL
    // CameraPower.cpp
    void parkPins();
#endif

    CameraStatus status{};
    bool boot_seen = false;
#if CONFIG_CAMERA_POWER_CONTROL
    CamLines camLines;
#endif
#endif
};

#endif  // CAMERAMANAGER_HPP