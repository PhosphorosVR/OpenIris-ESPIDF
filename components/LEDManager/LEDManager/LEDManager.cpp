#include "LEDManager.hpp"

const char *LED_MANAGER_TAG = "[LED_MANAGER]";

// Use a dedicated LEDC timer/channel for the external LED
#ifdef CONFIG_LED_EXTERNAL_CONTROL
static constexpr ledc_timer_t EXT_LED_LEDC_TIMER = LEDC_TIMER_1;
static constexpr ledc_channel_t EXT_LED_LEDC_CHANNEL = LEDC_CHANNEL_1;
#endif


ledStateMap_t LEDManager::ledStateMap = {
    {
        LEDStates_e::LedStateNone,
        {
            false,
            false,
            {{LED_OFF, 1000}},
        },
    },
    {
        LEDStates_e::LedStateStreaming,
        {
            false,
            true,
            {{LED_ON, 1000}},
        },
    },
    {
        LEDStates_e::LedStateStoppedStreaming,
        {
            false,
            true,
            {{LED_OFF, 1000}},
        },
    },
    {
        LEDStates_e::CameraError,
        {
            true,
            true,
            {{{LED_ON, 300}, {LED_OFF, 300}, {LED_ON, 300}, {LED_OFF, 300}}},
        },
    },
    {
        LEDStates_e::WiFiStateConnecting,
        {
            false,
            true,
            {{LED_ON, 400}, {LED_OFF, 400}},
        },
    },
    {
        LEDStates_e::WiFiStateConnected,
        {
            false,
            false,
            {{LED_ON, 200}, {LED_OFF, 200}, {LED_ON, 200}, {LED_OFF, 200}, {LED_ON, 200}, {LED_OFF, 200}, {LED_ON, 200}, {LED_OFF, 200}, {LED_ON, 200}, {LED_OFF, 200}},
        },
    },
    {
        LEDStates_e::WiFiStateError,
        {
            true,
            true,
            {{LED_ON, 200}, {LED_OFF, 100}, {LED_ON, 500}, {LED_OFF, 100}, {LED_ON, 200}},
        },
    },
};

LEDManager::LEDManager(gpio_num_t pin, gpio_num_t illumninator_led_pin,
                       QueueHandle_t ledStateQueue, std::shared_ptr<ProjectConfig> deviceConfig) : blink_led_pin(pin),
                                                                                                   illumninator_led_pin(illumninator_led_pin),
                                                                                                   ledStateQueue(ledStateQueue),
                                                                                                   currentState(LEDStates_e::LedStateNone),
                                                                                                   deviceConfig(deviceConfig)
{
}

void LEDManager::setup()
{
    ESP_LOGI(LED_MANAGER_TAG, "Setting up status led.");
    gpio_reset_pin(blink_led_pin);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(blink_led_pin, GPIO_MODE_OUTPUT);
    this->toggleLED(LED_OFF);

#ifdef CONFIG_LED_EXTERNAL_CONTROL
    ESP_LOGI(LED_MANAGER_TAG, "Setting up illuminator led.");
    const int freq = CONFIG_LED_EXTERNAL_PWM_FREQ;
#ifdef CONFIG_LED_EXTERNAL_CONTROL
    const auto resolution = LEDC_TIMER_8_BIT;
    const auto deviceConfig = this->deviceConfig->getDeviceConfig();

    const uint32_t dutyCycle = (deviceConfig.led_external_pwm_duty_cycle * 255) / 100;

    ESP_LOGI(LED_MANAGER_TAG, "Setting dutyCycle to: %lu ", dutyCycle);

#else
    (void)freq;
    (void)deviceConfig;
#endif
#endif

    ESP_LOGD(LED_MANAGER_TAG, "Done.");
}

void LEDManager::handleLED()
{
    if (!this->finishedPattern)
    {
        displayCurrentPattern();
        return;
    }

    if (xQueueReceive(this->ledStateQueue, &buffer, 10))
    {
        this->updateState(buffer);
    }
    else
    {
        // we've finished displaying the pattern, so let's check if it's repeatable and if so - reset it
        if (ledStateMap[this->currentState].isRepeatable || ledStateMap[this->currentState].isError)
        {
            this->currentPatternIndex = 0;
            this->finishedPattern = false;
        }
    }
}

void LEDManager::displayCurrentPattern()
{
    auto [state, delayTime] = ledStateMap[this->currentState].patterns[this->currentPatternIndex];
    this->toggleLED(state);
    // Optionally mirror error pattern to external LED (PWM) by toggling duty between 0% and current config
    mirrorExternalIfError(state);
    this->timeToDelayFor = delayTime;

    if (this->currentPatternIndex < ledStateMap[this->currentState].patterns.size() - 1)
        this->currentPatternIndex++;
    else
    {
        this->finishedPattern = true;
        this->toggleLED(LED_OFF);
    }
}

void LEDManager::updateState(const LEDStates_e newState)
{
    // If we've got an error state - that's it, keep repeating it indefinitely
    if (ledStateMap[this->currentState].isError)
        return;

    // Alternative (recoverable error states):
    // Allow recovery from error states by only blocking transitions when both, current and new states are error. Uncomment to enable recovery.
    // if (ledStateMap[this->currentState].isError && ledStateMap[newState].isError)
    //     return;

    // Only update when new state differs and is known.
    if (!ledStateMap.contains(newState))
        return;

    if (newState == this->currentState)
        return;

    this->currentState = newState;
    this->currentPatternIndex = 0;
    this->finishedPattern = false;
}

void LEDManager::toggleLED(const bool state) const
{
    gpio_set_level(blink_led_pin, state);
}

void LEDManager::mirrorExternalIfError(int state)
{
#if defined(CONFIG_LED_EXTERNAL_CONTROL) && defined(CONFIG_LED_DEBUG_USE_EXTERNAL)
    // Only mirror during error states
    if (!ledStateMap[this->currentState].isError)
        return;

    // Map LED_ON/LED_OFF to PWM duty values
    // Use configured duty for "ON" and 0 for "OFF"
    uint8_t configuredDuty = 50; // safe default
    if (this->deviceConfig)
    {
        configuredDuty = this->deviceConfig->getDeviceConfig().led_external_pwm_duty_cycle;
    }
    // Map pattern state to duty: ON -> configuredDuty, OFF -> 0
    const uint8_t targetDuty = (state == LED_ON) ? configuredDuty : 0;
    if (lastExternalDutyApplied != static_cast<int>(targetDuty))
    {
        // Use info-level only on setup, keep mirroring quiet to reduce stack/log pressure
        setExternalLEDDutyCycle(targetDuty);
        lastExternalDutyApplied = static_cast<int>(targetDuty);
    }
#else
    (void)state;
#endif
}

void LEDManager::setExternalLEDDutyCycle(uint8_t dutyPercent)
{
#ifdef CONFIG_LED_EXTERNAL_CONTROL
    const uint32_t dutyCycle = (static_cast<uint32_t>(dutyPercent) * 255) / 100;
    ESP_LOGD(LED_MANAGER_TAG, "Updating external LED duty to %u%% (raw %lu)", dutyPercent, dutyCycle);

    // Apply to LEDC hardware live
#if defined(LEDC_LOW_SPEED_MODE)
    // Apply to LEDC hardware live
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, EXT_LED_LEDC_CHANNEL, dutyCycle));
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, EXT_LED_LEDC_CHANNEL));
#else
    (void)dutyCycle;
#endif
#else
    (void)dutyPercent; // unused
    ESP_LOGW(LED_MANAGER_TAG, "CONFIG_LED_EXTERNAL_CONTROL not enabled; ignoring duty update");
#endif
}

void HandleLEDDisplayTask(void *pvParameter)
{
    auto *ledManager = static_cast<LEDManager *>(pvParameter);
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true)
    {
        ledManager->handleLED();
        const TickType_t delayTicks = pdMS_TO_TICKS(ledManager->getTimeToDelayFor());
        // Ensure at least 1 tick delay to yield CPU
        vTaskDelayUntil(&lastWakeTime, delayTicks > 0 ? delayTicks : 1);
    }
}
