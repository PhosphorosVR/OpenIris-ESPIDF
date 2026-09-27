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
#endif

   private:
    // CameraStatus.cpp
    void beginSetup();
    void endSetup(esp_err_t result);

    CameraStatus status{};
    bool boot_seen = false;
#if CONFIG_CAMERA_POWER_CONTROL
    CamLines camLines;
#endif
#endif
};

#endif  // CAMERAMANAGER_HPP