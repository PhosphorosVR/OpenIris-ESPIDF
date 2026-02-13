/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 * Copyright (c) 2023 Espressif
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "tusb.h"
#include "usb_descriptors.h"
#include "../include/usb_device_uvc.h"
#include <string.h> // memcpy, strlen

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
// Device descriptor: identifies this as a composite device using IAD for UVC
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    // Use Interface Association Descriptor (IAD) for Video
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = CONFIG_TUSB_VID,
    .idProduct = CONFIG_TUSB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
// String descriptor indices used in interface descriptors
#define STRID_LANGID 0
#define STRID_MANUFACTURER 1
#define STRID_PRODUCT 2
#define STRID_SERIAL 3
#define STRID_UVC_CAM1 4
// CDC interface string index used by TUD_CDC_DESCRIPTOR below
#define STRID_CDC 6

// Endpoint numbers for CDC
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
// Endpoint numbers for UVC video IN endpoints (device -> host)
#define EPNUM_CAM1_VIDEO_IN 0x83

// Single-size MJPEG bulk descriptors; we return either 320 or 240 variant at runtime
#define TUD_CAM1_VIDEO_CAPTURE_DESC_LEN (TUD_VIDEO_CAPTURE_DESC_MJPEG_BULK_LEN)

#define CONFIG_TOTAL_LEN_320 (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_CAM1_VIDEO_CAPTURE_DESC_LEN)
#define CONFIG_TOTAL_LEN_240 (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_CAM1_VIDEO_CAPTURE_DESC_LEN)

static uint8_t const desc_fs_configuration_320[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_320, 0, 200),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 6, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    // Camera 1, single-size MJPEG over BULK 320x320
    TUD_VIDEO_CAPTURE_DESCRIPTOR_MJPEG_BULK(STRID_UVC_CAM1, ITF_NUM_VIDEO_CONTROL, EPNUM_CAM1_VIDEO_IN,
                                            320, 320, CONFIG_UVC_CAM1_FRAMERATE,
                                            CFG_TUD_CAM1_VIDEO_STREAMING_EP_BUFSIZE),
};

static uint8_t const desc_fs_configuration_240[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_240, 0, 200),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 6, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    // Camera 1, single-size MJPEG over BULK 240x240
    TUD_VIDEO_CAPTURE_DESCRIPTOR_MJPEG_BULK(STRID_UVC_CAM1, ITF_NUM_VIDEO_CONTROL, EPNUM_CAM1_VIDEO_IN,
                                            240, 240, CONFIG_UVC_CAM1_FRAMERATE,
                                            CFG_TUD_CAM1_VIDEO_STREAMING_EP_BUFSIZE),
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; // for multiple configurations

    if (uvc_is_frame_profile_320())
    {
        return desc_fs_configuration_320;
    }
    return desc_fs_configuration_240;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// Array of pointers to string literals. Indices must match STRID_* above.
// NOTE: Indices must be contiguous up to the highest used index (STRID_CDC = 6)
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: Supported language: English (0x0409)
    CONFIG_TUSB_MANUFACTURER,   // 1: Manufacturer
    CONFIG_TUSB_PRODUCT,        // 2: Product (overridden by advertised name)
    CONFIG_TUSB_SERIAL_NUM,     // 3: Serial (overridden by get_serial_number())
    "UVC CAM1",                // 4: UVC Interface name for Cam1 (overridden by get_uvc_device_name())
    "CDC",                     // 5: placeholder (unused)
    "CDC Interface",           // 6: CDC Interface name (overridden to advertised name)
};

static uint16_t _desc_str[32];

__attribute__((weak)) const char *get_uvc_device_name(void)
{
    // Default UVC device name, can be overridden by application
    return "UVC OpenIris Camera";
}

__attribute__((weak)) const char *get_serial_number(void)
{
    // Default serial number, can be overridden by application (e.g., chip ID)
    return CONFIG_TUSB_SERIAL_NUM;
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
        {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        // Allow dynamic overrides for specific indices
        if (index == STRID_SERIAL)
            str = get_serial_number();
        // Unify all user-visible names (Product, UVC interface, CDC interface) to advertised name
        if (index == STRID_UVC_CAM1 || index == STRID_PRODUCT || index == STRID_CDC)
            str = get_uvc_device_name();
        if (str == NULL)
            str = string_desc_arr[index];

        // Cap at max char
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31)
        {
            chr_count = 31;
        }

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}