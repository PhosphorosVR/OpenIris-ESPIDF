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
#include <atomic>
#include "CameraTypes.hpp"
#if CONFIG_CAMERA_POWER_CONTROL
#include "CamLines.hpp"
#endif
#endif

// Frames for consumers (UVC). With the recovery they pass a gate that the recovery closes
// before it takes the camera down; without it they are the plain driver calls.
#if CONFIG_CAMERA_RECOVERY_ENABLE
camera_fb_t* cameraAcquireFrame();
void cameraReleaseFrame(camera_fb_t* fb);
uint16_t cameraSensorPid();
#else
__attribute__((always_inline)) static inline camera_fb_t* cameraAcquireFrame()
{
    return esp_camera_fb_get();
}
__attribute__((always_inline)) static inline void cameraReleaseFrame(camera_fb_t* fb)
{
    esp_camera_fb_return(fb);
}
__attribute__((always_inline)) static inline uint16_t cameraSensorPid()
{
    auto* sensor = esp_camera_sensor_get();
    return sensor ? sensor->id.PID : 0;
}
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
    uint16_t sensorPid() const
    {
        return status.pid;
    }
#if CONFIG_CAMERA_POWER_CONTROL
    const CamLines& lines() const
    {
        return camLines;
    }
#endif
    // CameraCycle.cpp: restart the camera on the camera task and wait for the result.
    // false when another cycle is running; the report is left untouched then.
    bool runCycleBlocking(const CycleRequest& request, RecoveryTrigger trigger, CycleReport& report);
#if CONFIG_CAMERA_RECOVERY_ENABLE
    // Consumer side (CameraGate.cpp): a request that got no frame.
    void onFrameMissing();
#endif

   private:
    // CameraStatus.cpp
    void beginSetup();
    void endSetup(esp_err_t result);
    // CameraCycle.cpp
    void startCameraTask();
    static void cameraTask(void* arg);
    CycleReport runCycle(const CycleRequest& request);
    void parkPins();
    void takeDriverDown();
#if CONFIG_CAMERA_RECOVERY_ENABLE
    int applyFrameSize(framesize_t frameSize);
#endif

    CameraStatus status{};
    bool boot_seen = false;
    bool in_cycle = false;
    QueueHandle_t cycle_queue = nullptr;
    std::atomic<bool> cycle_busy{false};
#if CONFIG_CAMERA_RECOVERY_ENABLE
    std::atomic<uint32_t> frames_missing{0};
    framesize_t requested_framesize = FRAMESIZE_INVALID;
#endif
#if CONFIG_CAMERA_POWER_CONTROL
    CamLines camLines;
#endif
#endif
};

#endif  // CAMERAMANAGER_HPP