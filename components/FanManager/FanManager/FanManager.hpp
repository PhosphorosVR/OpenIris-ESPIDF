#pragma once
#ifndef _FANMANAGER_HPP_
#define _FANMANAGER_HPP_

#include "driver/gpio.h"
#include "driver/ledc.h"
#include <ProjectConfig.hpp>
#include <cstdint>
#include <esp_log.h>
#include <memory>

class FanManager
{
   public:
    FanManager(gpio_num_t fan_pin, std::shared_ptr<ProjectConfig> deviceConfig);

    void setup();
    void setFanDutyCycle(uint8_t dutyPercent);
    uint8_t getFanDutyCycle() const;

   private:
    gpio_num_t fan_pin;
    std::shared_ptr<ProjectConfig> deviceConfig;
    bool initialized = false;
};

#endif
