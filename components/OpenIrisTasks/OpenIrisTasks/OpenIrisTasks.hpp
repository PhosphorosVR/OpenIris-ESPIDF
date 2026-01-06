#pragma once
#ifndef OPENIRISTASKS_HPP
#define OPENIRISTASKS_HPP

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "helpers.hpp"

namespace OpenIrisTasks
{
void ScheduleRestart(int milliseconds);
};

#endif