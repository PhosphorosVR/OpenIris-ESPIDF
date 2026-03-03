#include "UVCStream.hpp"

#ifdef CONFIG_GENERAL_INCLUDE_UVC_MODE
#include <atomic>
#include <cstdio>  // for snprintf
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* UVC_STREAM_TAG = "[UVC DEVICE]";

// Tracks whether a frame has been handed to TinyUSB and not yet returned.
// Atomic because video_task and TinyUSB task (tud_suspend_cb) access it concurrently.
static std::atomic<bool> s_frame_inflight{false};
// Set by camera_stop_cb so camera_fb_get_cb skips new acquisitions during USB suspend.
static std::atomic<bool> s_stopping{false};

extern "C"
{
    static char serial_number_str[13];

    const char* get_uvc_device_name()
    {
        return deviceConfig->getMDNSConfig().hostname.c_str();
    }

    const char* get_serial_number(void)
    {
        if (serial_number_str[0] == '\0')
        {
            uint8_t mac_address[6];
            esp_err_t result = esp_efuse_mac_get_default(mac_address);
            if (result != ESP_OK)
            {
                ESP_LOGE(UVC_STREAM_TAG, "Failed to get MAC address of the board, returning default serial number");
                return CONFIG_TUSB_SERIAL_NUM;
            }

            // 12 hex chars without separators
            snprintf(serial_number_str, sizeof(serial_number_str), "%02X%02X%02X%02X%02X%02X", mac_address[0], mac_address[1], mac_address[2], mac_address[3],
                     mac_address[4], mac_address[5]);
        }
        return serial_number_str;
    }
}

// single definition of shared framebuffer storage
UVCStreamHelpers::fb_t UVCStreamHelpers::s_fb = {};

static void reset_pacing_state()
{
    s_frame_inflight.store(false);
}

static esp_err_t UVCStreamHelpers::camera_start_cb(uvc_format_t format, int width, int height, int rate, void* cb_ctx)
{
    ESP_LOGI(UVC_STREAM_TAG, "Camera Start");
    ESP_LOGI(UVC_STREAM_TAG, "Format: %d, width: %d, height: %d, rate: %d", format, width, height, rate);
    framesize_t frame_size = FRAMESIZE_240X240;
    auto* sensor = esp_camera_sensor_get();
    uint16_t pid = sensor ? sensor->id.PID : 0;

    if (format != UVC_FORMAT_JPEG)
    {
        ESP_LOGE(UVC_STREAM_TAG, "Only support MJPEG format");
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Sensor-based allowance: OV3660 only 320x320; OV2640 only 240x240; others 240/320
    if (pid == OV3660_PID)
    {
        if (width == 320 && height == 320)
        {
            frame_size = FRAMESIZE_320X320;
        }
        else
        {
            ESP_LOGE(UVC_STREAM_TAG, "OV3660 requires 320x320, requested %dx%d not allowed", width, height);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    else if (pid == OV2640_PID)
    {
        if (width == 240 && height == 240)
        {
            frame_size = FRAMESIZE_240X240;
        }
        else
        {
            ESP_LOGE(UVC_STREAM_TAG, "OV2640 limited to 240x240 for UVC, requested %dx%d", width, height);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    else
    {
        // Fallback: accept 320 or 240
        if (width == 320 && height == 320)
        {
            frame_size = FRAMESIZE_320X320;
        }
        else if (width == 240 && height == 240)
        {
            frame_size = FRAMESIZE_240X240;
        }
        else
        {
            ESP_LOGE(UVC_STREAM_TAG, "Unsupported frame size %dx%d", width, height);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }

    cameraHandler->setCameraResolution(frame_size);

    s_stopping.store(false);
    reset_pacing_state();
    SendStreamEvent(eventQueue, StreamState_e::Stream_ON);

    return ESP_OK;
}

static void UVCStreamHelpers::camera_stop_cb(void* cb_ctx)
{
    (void)cb_ctx;
    s_stopping.store(true);

    // Always release camera FB to prevent frame buffer leaks.
    // Even if a USB transfer is in flight the DMA has already read the data
    // from DRAM so returning the buffer here is safe.
    if (s_fb.cam_fb_p)
    {
        esp_camera_fb_return(s_fb.cam_fb_p);
        s_fb.cam_fb_p = nullptr;
    }

    reset_pacing_state();

    SendStreamEvent(eventQueue, StreamState_e::Stream_OFF);
}

static uvc_fb_t* UVCStreamHelpers::camera_fb_get_cb(void* cb_ctx)
{
    auto* mgr = static_cast<UVCStreamManager*>(cb_ctx);

    // Guard against requesting a new frame while previous is still in flight
    // or while the host has signalled a stop via tud_suspend_cb.
    if (s_frame_inflight.load() || s_stopping.load())
    {
        return nullptr;
    }

    // NOTE: Frame-rate pacing is already handled by video_task (interval_ms).
    // A second, independent pacing layer here was causing double-throttling
    // and excessive frame drops that could starve the USB host.

    // Acquire a fresh frame
    camera_fb_t* cam_fb = esp_camera_fb_get();
    if (!cam_fb)
    {
        return nullptr;
    }

    s_fb.cam_fb_p = cam_fb;
    s_fb.uvc_fb.buf = cam_fb->buf;
    s_fb.uvc_fb.len = cam_fb->len;
    s_fb.uvc_fb.width = cam_fb->width;
    s_fb.uvc_fb.height = cam_fb->height;
    s_fb.uvc_fb.format = UVC_FORMAT_JPEG;
    s_fb.uvc_fb.timestamp = cam_fb->timestamp;

    // Validate size fits into transfer buffer
    if (mgr && s_fb.uvc_fb.len > mgr->getUvcBufferSize())
    {
        ESP_LOGE(UVC_STREAM_TAG, "Frame size %d exceeds UVC buffer size %u", (int)s_fb.uvc_fb.len, (unsigned)mgr->getUvcBufferSize());
        esp_camera_fb_return(cam_fb);
        s_fb.cam_fb_p = nullptr;
        return nullptr;
    }

    s_frame_inflight.store(true);
    return &s_fb.uvc_fb;
}

static void UVCStreamHelpers::camera_fb_return_cb(uvc_fb_t* fb, void* cb_ctx)
{
    (void)cb_ctx;
    // fb may be NULL when called from tud_video_frame_xfer_complete_cb
    // (zero-copy path: video_task no longer copies into a separate buffer,
    //  so the camera FB is returned here after USB finishes the transfer).
    if (s_fb.cam_fb_p)
    {
        esp_camera_fb_return(s_fb.cam_fb_p);
        s_fb.cam_fb_p = nullptr;
    }
    s_frame_inflight.store(false);
}

esp_err_t UVCStreamManager::setup()
{
    ESP_LOGI(UVC_STREAM_TAG, "Setting up UVC Stream");

    // Select descriptor/profile based on detected sensor (OV3660 -> 320, OV2640 -> 240)
    bool use_320 = true;
    if (auto* sensor = esp_camera_sensor_get())
    {
        if (sensor->id.PID == OV2640_PID)
        {
            use_320 = false;
        }
    }
    uvc_select_frame_profile(use_320);

    // Allocate a fixed-size transfer buffer (compile-time constant)
    uvc_buffer_size = UVCStreamManager::UVC_MAX_FRAMESIZE_SIZE;
    if (uvc_buffer != nullptr)
    {
        free(uvc_buffer);
    }
    uvc_buffer = static_cast<uint8_t*>(malloc(uvc_buffer_size));
    if (uvc_buffer == nullptr)
    {
        ESP_LOGE(UVC_STREAM_TAG, "Allocating buffer for UVC Device failed");
        return ESP_FAIL;
    }

    uvc_device_config_t config = {
        .uvc_buffer = uvc_buffer,
        .uvc_buffer_size = UVCStreamManager::UVC_MAX_FRAMESIZE_SIZE,
        .start_cb = UVCStreamHelpers::camera_start_cb,
        .fb_get_cb = UVCStreamHelpers::camera_fb_get_cb,
        .fb_return_cb = UVCStreamHelpers::camera_fb_return_cb,
        .stop_cb = UVCStreamHelpers::camera_stop_cb,
        .cb_ctx = this,
    };

    esp_err_t ret = uvc_device_config(0, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(UVC_STREAM_TAG, "Configuring UVC Device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(UVC_STREAM_TAG, "Configured UVC Device");

    ESP_LOGI(UVC_STREAM_TAG, "Initializing UVC Device");
    ret = uvc_device_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(UVC_STREAM_TAG, "Initializing UVC Device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(UVC_STREAM_TAG, "Initialized UVC Device");

    // Initial state is OFF
    SendStreamEvent(eventQueue, StreamState_e::Stream_OFF);

    return ESP_OK;
}

esp_err_t UVCStreamManager::start()
{
    ESP_LOGI(UVC_STREAM_TAG, "Starting UVC streaming");
    // UVC device is already initialized in setup(), just log that we're starting
    return ESP_OK;
}

#endif