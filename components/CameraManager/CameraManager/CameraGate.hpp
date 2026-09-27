#pragma once
#ifndef CAMERAGATE_HPP
#define CAMERAGATE_HPP

#include "sdkconfig.h"

#if CONFIG_CAMERA_RECOVERY_ENABLE
#include <cstdint>

// Internal to the camera manager. Consumers use cameraAcquireFrame()/cameraReleaseFrame().
class CameraManager;

void cameraGateInit(CameraManager* owner);
// Stops handing out frames and waits until every frame is back and no consumer is
// inside the driver. false: timeout, the gate is open again and nothing was touched.
bool cameraGateClose(uint32_t drain_timeout_ms);
void cameraGateOpen();

#endif  // CONFIG_CAMERA_RECOVERY_ENABLE
#endif  // CAMERAGATE_HPP
