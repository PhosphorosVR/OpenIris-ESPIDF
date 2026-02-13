/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "sdkconfig.h"

#ifdef CONFIG_FORMAT_MJPEG_CAM1
#define FORMAT_MJPEG_CAM1 1
#endif

#ifdef CONFIG_UVC_CAM1_MULTI_FRAMESIZE
// If enable, add VGA and HVGA to list
#define UVC_CAM1_FRAME_MULTI 1
#endif

#define UVC_CAM1_FRAME_WIDTH CONFIG_UVC_CAM1_FRAMESIZE_WIDTH
#define UVC_CAM1_FRAME_HEIGHT CONFIG_UVC_CAM1_FRAMESIZE_HEIGT
#define UVC_CAM1_FRAME_RATE CONFIG_UVC_CAM1_FRAMERATE

#ifdef CONFIG_UVC_MODE_BULK_CAM1
#define UVC_CAM1_BULK_MODE
#endif

#ifndef UVC_CAM2_FRAME_WIDTH
#define UVC_CAM2_FRAME_WIDTH UVC_CAM1_FRAME_WIDTH
#endif

#ifndef UVC_CAM2_FRAME_HEIGHT
#define UVC_CAM2_FRAME_HEIGHT UVC_CAM1_FRAME_HEIGHT
#endif

#ifndef UVC_CAM2_FRAME_RATE
#define UVC_CAM2_FRAME_RATE UVC_CAM1_FRAME_RATE
#endif

typedef struct
{
    int width;
    int height;
    int rate;
} uvc_frame_info_t;

#define UVC_FRAME_NUM 1

extern const uvc_frame_info_t UVC_FRAMES_INFO_320[UVC_FRAME_NUM];
extern const uvc_frame_info_t UVC_FRAMES_INFO_240[UVC_FRAME_NUM];
