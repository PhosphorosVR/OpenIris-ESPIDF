#include "CameraManager.hpp"

const char* CAMERA_MANAGER_TAG = "[CAMERA_MANAGER]";

struct CameraProfile
{
    framesize_t default_framesize;  // default resolution for this sensor
    int brightness;                 // image brightness offset
    int contrast;                   // image contrast
    int saturation;                 // color saturation (negative reduces chroma noise)
    int whitebal;                   // auto white balance on/off
    int awb_gain;                   // AWB gain on/off
    int wb_mode;                    // preset white balance mode
    int exposure_ctrl;              // auto exposure on/off
    int aec2;                       // advanced auto exposure on/off
    int ae_level;                   // exposure bias
    int aec_value;                  // manual exposure value when auto is off
    int gain_ctrl;                  // auto gain control on/off
    int agc_gain;                   // manual gain value when auto is off
    int gainceiling;                // max gain when auto gain is on
    int bpc;                        // black pixel correction on/off
    int wpc;                        // white pixel correction on/off
    int dcw;                        // downsize/crop window on/off
    int raw_gma;                    // raw gamma on/off
    int lenc;                       // lens correction on/off
    int colorbar;                   // test pattern on/off
    int special_effect;             // special effect (e.g., grayscale)
};

static const CameraProfile PROFILE_OV2640_BW = {
    .default_framesize = FRAMESIZE_240X240,
    .brightness = 2,
    .contrast = 2,
    .saturation = -2,
    .whitebal = 1,
    .awb_gain = 0,
    .wb_mode = 0,
    .exposure_ctrl = 0,
    .aec2 = 0,
    .ae_level = 0,
    .aec_value = 300,
    .gain_ctrl = 0,
    .agc_gain = 2,
    .gainceiling = 6,
    .bpc = 1,
    .wpc = 1,
    .dcw = 0,
    .raw_gma = 1,
    .lenc = 0,
    .colorbar = 0,
    .special_effect = 2,  // grayscale
};

static const CameraProfile PROFILE_OV3660_BW = {
    .default_framesize = FRAMESIZE_320X320,
    .brightness = 0,
    .contrast = 2,
    .saturation = -2,
    .whitebal = 0,
    .awb_gain = 0,
    .wb_mode = 0,
    .exposure_ctrl = 0,
    .aec2 = 0,
    .ae_level = -1,
    .aec_value = 250,
    .gain_ctrl = 0,
    .agc_gain = 2,
    .gainceiling = 4,
    .bpc = 1,
    .wpc = 1,
    .dcw = 0,
    .raw_gma = 1,
    .lenc = 1,
    .colorbar = 0,
    .special_effect = 2,  // grayscale
};

static const CameraProfile* select_profile(sensor_t* sensor)
{
    if (!sensor)
    {
        return &PROFILE_OV2640_BW;
    }
    switch (sensor->id.PID)
    {
        case OV3660_PID:
            return &PROFILE_OV3660_BW;
        default:
            return &PROFILE_OV2640_BW;
    }
}

CameraManager::CameraManager(std::shared_ptr<ProjectConfig> projectConfig, QueueHandle_t eventQueue) : projectConfig(projectConfig), eventQueue(eventQueue)
{
    sensor_mutex = xSemaphoreCreateMutex();
}

void CameraManager::setupCameraPinout()
{
    // Workaround for espM5SStack not having a defined camera
#ifdef CONFIG_CAMERA_MODULE_NAME
    ESP_LOGI(CAMERA_MANAGER_TAG, "[Camera]: Camera module is %s", CONFIG_CAMERA_MODULE_NAME);
#else
    ESP_LOGI(CAMERA_MANAGER_TAG, "[Camera]: Camera module is undefined");
#endif

    // camera external clock signal frequencies
    // 10000000 stable
    // 16500000 optimal freq on ESP32-CAM (default)
    // 20000000 max freq on ESP32-CAM
    // 24000000 optimal freq on ESP32-S3 // 23MHz same fps
    int xclk_freq_hz = CONFIG_CAMERA_WIFI_XCLK_FREQ;

#if CONFIG_CAMERA_MODULE_ESP_EYE
    /* IO13, IO14 is designed for JTAG by default,
     * to use it as generalized input,
     * firstly declare it as pullup input
     **/
    gpio_reset_pin(13);
    gpio_reset_pin(14);
    gpio_set_direction(13, GPIO_MODE_INPUT);
    gpio_set_direction(14, GPIO_MODE_INPUT);
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(14, GPIO_PULLUP_ONLY);
    ESP_LOGI(CAMERA_MANAGER_TAG, "ESP_EYE");
#elif CONFIG_CAMERA_MODULE_CAM_BOARD
    /* IO13, IO14 is designed for JTAG by default,
     * to use it as generalized input,
     * firstly declare it as pullup input
     **/

    gpio_reset_pin(13);
    gpio_reset_pin(14);
    gpio_set_direction(13, GPIO_MODE_INPUT);
    gpio_set_direction(14, GPIO_MODE_INPUT);
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(14, GPIO_PULLUP_ONLY);

    ESP_LOGI(CAMERA_MANAGER_TAG, "CAM_BOARD");
#endif
#if CONFIG_GENERAL_INCLUDE_UVC_MODE
    xclk_freq_hz = CONFIG_CAMERA_USB_XCLK_FREQ_DEFAULT;
#endif

    config = {
        .pin_pwdn = CONFIG_PWDN_GPIO_NUM,      // CAM_PIN_PWDN,
        .pin_reset = CONFIG_RESET_GPIO_NUM,    // CAM_PIN_RESET,
        .pin_xclk = CONFIG_XCLK_GPIO_NUM,      // CAM_PIN_XCLK,
        .pin_sccb_sda = CONFIG_SIOD_GPIO_NUM,  // CAM_PIN_SIOD,
        .pin_sccb_scl = CONFIG_SIOC_GPIO_NUM,  // CAM_PIN_SIOC,

        .pin_d7 = CONFIG_Y9_GPIO_NUM,        /// CAM_PIN_D7,
        .pin_d6 = CONFIG_Y8_GPIO_NUM,        /// CAM_PIN_D6,
        .pin_d5 = CONFIG_Y7_GPIO_NUM,        // CAM_PIN_D5,
        .pin_d4 = CONFIG_Y6_GPIO_NUM,        // CAM_PIN_D4,
        .pin_d3 = CONFIG_Y5_GPIO_NUM,        // CAM_PIN_D3,
        .pin_d2 = CONFIG_Y4_GPIO_NUM,        // CAM_PIN_D2,
        .pin_d1 = CONFIG_Y3_GPIO_NUM,        // CAM_PIN_D1,
        .pin_d0 = CONFIG_Y2_GPIO_NUM,        // CAM_PIN_D0,
        .pin_vsync = CONFIG_VSYNC_GPIO_NUM,  // CAM_PIN_VSYNC,
        .pin_href = CONFIG_HREF_GPIO_NUM,    // CAM_PIN_HREF,
        .pin_pclk = CONFIG_PCLK_GPIO_NUM,    // CAM_PIN_PCLK,

        .xclk_freq_hz = xclk_freq_hz,  // Set in config
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,   // YUV422,GRAYSCALE,RGB565,JPEG
        .frame_size = FRAMESIZE_320X320,  // QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has
                                          // improved a lot, but JPEG mode always gives better frame rates.

        .jpeg_quality = 8,  // 0-63, for OV series camera sensors, lower number means higher quality // Below 6 stability problems
        .fb_count = 3,      // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
        .fb_location = CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_LATEST,  // was CAMERA_GRAB_LATEST; new mode reduces frame skips at cost of minor latency
    };
}

void CameraManager::setupCameraSensor()
{
    ESP_LOGI(CAMERA_MANAGER_TAG, "Setting up camera sensor");

    camera_sensor = esp_camera_sensor_get();

    // OV2640-only: fixes corrupted jpegs, https://github.com/espressif/esp32-camera/issues/203
    // documentation https://www.uctronics.com/download/cam_module/OV2640DS.pdf
    // These registers (bank 0x00, reg 0xD3) have a different meaning on OV3660
    // and writing them blindly can destabilise the sensor clock.
    if (camera_sensor && camera_sensor->id.PID == OV2640_PID)
    {
        camera_sensor->set_reg(camera_sensor, 0xff, 0xff,
                               0x00);                          // banksel
        camera_sensor->set_reg(camera_sensor, 0xd3, 0xff, 5);  // clock divider
    }

    const CameraProfile* profile = select_profile(camera_sensor);

    auto apply_profile = [](sensor_t* s, const CameraProfile* p) {
        if (!s || !p)
        {
            return;
        }
        s->set_brightness(s, p->brightness);
        s->set_contrast(s, p->contrast);
        s->set_saturation(s, p->saturation);

        s->set_whitebal(s, p->whitebal);
        s->set_awb_gain(s, p->awb_gain);
        s->set_wb_mode(s, p->wb_mode);

        s->set_exposure_ctrl(s, p->exposure_ctrl);
        s->set_aec2(s, p->aec2);
        s->set_ae_level(s, p->ae_level);
        s->set_aec_value(s, p->aec_value);

        s->set_gain_ctrl(s, p->gain_ctrl);
        s->set_agc_gain(s, p->agc_gain);
        s->set_gainceiling(s, static_cast<gainceiling_t>(p->gainceiling));

        s->set_bpc(s, p->bpc);
        s->set_wpc(s, p->wpc);
        s->set_dcw(s, p->dcw);

        s->set_raw_gma(s, p->raw_gma);
        s->set_lenc(s, p->lenc);
        s->set_colorbar(s, p->colorbar);
        s->set_special_effect(s, p->special_effect);
    };

    apply_profile(camera_sensor, profile);

    framesize_t sensor_default = profile ? profile->default_framesize : FRAMESIZE_240X240;
    if (camera_sensor)
    {
        ESP_LOGI(CAMERA_MANAGER_TAG, "Applying sensor default framesize %d for PID 0x%02x", sensor_default, camera_sensor->id.PID);
        camera_sensor->set_framesize(camera_sensor, sensor_default);

        // OV3660 PLL override: the stock driver's set_framesize() configures the
        // PLL for ~42 fps at 27 MHz XCLK (mult=30, pre_div=3 → SYSCLK=67.5 MHz).
        // We override with mult=29 for improved stability while staying within spec.
        //   REFIN  = 27/3      = 9 MHz       (spec 4-13.5 MHz)    OK
        //   VCO    = 9*29      = 261 MHz     (spec 150-500 MHz)   OK
        //   PLLCLK = 261/1/1   = 261 MHz     (sys_div=1, seld5=÷1)
        //   SYSCLK = PLLCLK/4  = 65.25 MHz   (fixed ÷4, drives JPEG encoder & frame timing)
        //   PCLK   = PLLCLK/2/15 = 8.7 MHz   (pixel clock to ESP32)
        //
        // _set_pll() public API param order (different from internal!):
        //   bypass, mult, sys_div, root_2x, pre_div, seld5, pclk_manual, pclk_div
        if (camera_sensor->id.PID == OV3660_PID &&
            camera_sensor->pixformat == PIXFORMAT_JPEG &&
            config.xclk_freq_hz >= 24000000)
        {
            ESP_LOGI(CAMERA_MANAGER_TAG, "OV3660: applying PLL override (mult=29, pre_div=3, pclk_div=15, XCLK=%lu Hz)",
                     static_cast<unsigned long>(config.xclk_freq_hz));
            camera_sensor->set_pll(camera_sensor, 0, 29, 1, 0, 3, 0, 1, 15);
        }
    }
    ESP_LOGI(CAMERA_MANAGER_TAG, "Setting up camera sensor done");
}

bool CameraManager::setupCamera()
{
    ESP_LOGI(CAMERA_MANAGER_TAG, "Setting up camera pinout");
    this->setupCameraPinout();
    ESP_LOGI(CAMERA_MANAGER_TAG, "Initializing camera...");

    if (auto const hasCameraBeenInitialized = esp_camera_init(&config); hasCameraBeenInitialized == ESP_OK)
    {
        ESP_LOGI(CAMERA_MANAGER_TAG, "Camera initialized: %s \r\n", esp_err_to_name(hasCameraBeenInitialized));

        constexpr auto event = SystemEvent{EventSource::CAMERA, CameraState_e::Camera_Success};
        xQueueSend(this->eventQueue, &event, 10);
    }
    else
    {
        ESP_LOGE(CAMERA_MANAGER_TAG, "Camera initialization failed with error: %s \r\n", esp_err_to_name(hasCameraBeenInitialized));
        ESP_LOGE(CAMERA_MANAGER_TAG,
                 "Camera most likely not seated properly in the socket. "
                 "Please "
                 "fix the "
                 "camera and reboot the device.\r\n");
        constexpr auto event = SystemEvent{EventSource::CAMERA, CameraState_e::Camera_Error};
        xQueueSend(this->eventQueue, &event, 10);
        return false;
    }

    // Per-sensor XCLK override applied after detection so SCCB probe stays stable.
    if (auto* detected_sensor = esp_camera_sensor_get())
    {
        auto* info = esp_camera_sensor_get_info(&detected_sensor->id);
        const uint32_t requested_xclk = [detected_sensor]() {
            switch (detected_sensor->id.PID)
            {
                case OV2640_PID:
                    return static_cast<uint32_t>(CONFIG_CAMERA_XCLK_FREQ_OV2640_OVERRIDE);
                case OV3660_PID:
                    return static_cast<uint32_t>(CONFIG_CAMERA_XCLK_FREQ_OV3660_OVERRIDE);
                default:
                    return static_cast<uint32_t>(0);
            }
        }();

        if (requested_xclk > 0)
        {
            uint32_t mhz = requested_xclk / 1000000U;
            if (mhz == 0)
            {
                ESP_LOGW(CAMERA_MANAGER_TAG, "XCLK override %lu Hz too low; keeping %lu Hz",
                         static_cast<unsigned long>(requested_xclk), static_cast<unsigned long>(config.xclk_freq_hz));
            }
            else
            {
                if ((requested_xclk % 1000000U) != 0)
                {
                    ESP_LOGW(CAMERA_MANAGER_TAG,
                             "XCLK override %lu Hz not multiple of 1MHz; rounding to %lu MHz (driver granularity)",
                             static_cast<unsigned long>(requested_xclk), static_cast<unsigned long>(mhz));
                }

                if (detected_sensor->set_xclk(detected_sensor, config.ledc_timer, static_cast<int>(mhz)) == 0)
                {
                    config.xclk_freq_hz = mhz * 1000000U;
                    ESP_LOGI(CAMERA_MANAGER_TAG,
                             "Camera %s (PID 0x%02x): applied XCLK override %lu Hz, waiting for PLL relock...",
                             info ? info->name : "unknown", detected_sensor->id.PID,
                             static_cast<unsigned long>(config.xclk_freq_hz));
                    // The sensor PLL must re-lock after an XCLK frequency change.
                    // Without this delay, subsequent I2C (SCCB) register writes
                    // fail with NACK because the sensor's internal clocks are
                    // still settling.  100 ms is conservative; typical PLL lock
                    // time for OV sensors is 10-50 ms.
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                else
                {
                    ESP_LOGW(CAMERA_MANAGER_TAG,
                             "Camera %s (PID 0x%02x): failed to apply XCLK override %lu Hz, keeping %lu Hz",
                             info ? info->name : "unknown", detected_sensor->id.PID,
                             static_cast<unsigned long>(requested_xclk), static_cast<unsigned long>(config.xclk_freq_hz));
                }
            }
        }
        else
        {
            ESP_LOGI(CAMERA_MANAGER_TAG,
                     "Camera %s (PID 0x%02x): using default XCLK %lu Hz",
                     info ? info->name : "unknown", detected_sensor->id.PID,
                     static_cast<unsigned long>(config.xclk_freq_hz));
        }
    }
    else
    {
        ESP_LOGW(CAMERA_MANAGER_TAG, "Camera sensor handle unavailable for XCLK override");
    }

#if CONFIG_GENERAL_INCLUDE_UVC_MODE
    const auto temp_sensor = esp_camera_sensor_get();

    // Thanks to lick_it, we discovered that OV5640 likes to overheat when
    // running at higher than usual xclk frequencies.
    // Hence, why we're limiting the faster ones for OV2640
    if (const auto camera_id = temp_sensor->id.PID; camera_id == OV5640_PID)
    {
        config.xclk_freq_hz = OV5640_XCLK_FREQ_HZ;
        esp_camera_deinit();
        esp_camera_init(&config);
    }

#endif

    this->setupCameraSensor();
    return true;
}

void CameraManager::loadConfigData()
{
    if (!camera_sensor) return;
    ESP_LOGD(CAMERA_MANAGER_TAG, "Loading camera config data");
    CameraConfig_t cameraConfig = projectConfig->getCameraConfig();
    this->setHFlip(cameraConfig.href);
    this->setVFlip(cameraConfig.vflip);
    framesize_t requested_frame = static_cast<framesize_t>(cameraConfig.framesize);
    const CameraProfile* profile = select_profile(camera_sensor);
    if (camera_sensor && profile)
    {
        requested_frame = profile->default_framesize;
    }
    this->setCameraResolution(requested_frame);
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    camera_sensor->set_quality(camera_sensor, cameraConfig.quality);
    camera_sensor->set_agc_gain(camera_sensor, cameraConfig.brightness);
    xSemaphoreGive(sensor_mutex);
    ESP_LOGD(CAMERA_MANAGER_TAG, "Loading camera config data done");
}

int CameraManager::setCameraResolution(const framesize_t frameSize)
{
    if (!camera_sensor) return -1;
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    int ret = -1;
    if (camera_sensor->pixformat == PIXFORMAT_JPEG)
    {
        ret = camera_sensor->set_framesize(camera_sensor, frameSize);

        if (ret == 0 &&
            camera_sensor->id.PID == OV3660_PID &&
            config.xclk_freq_hz >= 24000000)
        {
            camera_sensor->set_pll(camera_sensor, 0, 29, 1, 0, 3, 0, 1, 15);
        }
    }
    xSemaphoreGive(sensor_mutex);
    return ret;
}

int CameraManager::setVFlip(const int direction)
{
    if (!camera_sensor) return -1;
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    int ret = camera_sensor->set_vflip(camera_sensor, direction);
    xSemaphoreGive(sensor_mutex);
    return ret;
}

int CameraManager::setHFlip(const int direction)
{
    if (!camera_sensor) return -1;
    xSemaphoreTake(sensor_mutex, portMAX_DELAY);
    int ret = camera_sensor->set_hmirror(camera_sensor, direction);
    xSemaphoreGive(sensor_mutex);
    return ret;
}

int CameraManager::setVieWindow(int offsetX, int offsetY, int outputX, int outputY)
{
    // todo safariMonkey made a PoC, implement it here
    return 0;
}