/*
 * USB protocol descriptors and the XInput class-driver shape are adapted from
 * the MIT-licensed GP2040-CE and Adafruit_TinyUSB_XInput projects.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity
 * SPDX-FileCopyrightText: Copyright (c) 2019 Ha Thach for Adafruit Industries
 * SPDX-License-Identifier: MIT
 */

#include "taiko_usb.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "class/hid/hid_device.h"
#include "device/usbd_pvt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "taiko_ps4_auth.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#define USB_VID 0x4869
#define USB_PID 0x4869
#define USB_ENDPOINT_SIZE 64

#define XINPUT_INTERFACE_SUBCLASS 0x5d
#define XINPUT_INTERFACE_PROTOCOL 0x01
#define XINPUT_IN_ENDPOINT 0x81
#define XINPUT_OUT_ENDPOINT 0x02
#define XINPUT_ENDPOINT_SIZE 32
#define XINPUT_INTERFACE_DESCRIPTOR_LENGTH (9 + 16 + 7 + 7)

#define MICROSOFT_VENDOR_REQUEST 0x01
#define MS_OS_20_DESCRIPTOR_LENGTH 0xb2
#define BOS_TOTAL_LENGTH (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

enum {
    USB_INTERFACE_GAMEPAD = 0,
    USB_INTERFACE_COUNT,
};

static taiko_controller_mode_t s_mode = TAIKO_CONTROLLER_MODE_ARCADE;
static taiko_input_snapshot_t s_last_input;
static portMUX_TYPE s_last_input_lock = portMUX_INITIALIZER_UNLOCKED;
static taiko_arcade_report_t s_arcade_report;
static taiko_xinput_report_t s_xinput_report;
static taiko_switch_report_t s_switch_report;
static taiko_ps4_report_t s_ps4_report;
static uint8_t s_ps4_report_counter;
static uint8_t s_ps4_nonce_id;

static uint8_t s_xinput_in_endpoint;
static uint8_t s_xinput_out_endpoint;
static uint8_t s_xinput_out_buffer[XINPUT_ENDPOINT_SIZE];
static const char *TAG = "taiko_usb";

static const char kLanguage[] = {0x09, 0x04, 0x00};

static const tusb_desc_device_t kArcadeDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const tusb_desc_device_t kXinputDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID + 1,
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

// Nintendo Switch-compatible HORI Pokken controller identity.
static const tusb_desc_device_t kSwitchDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x0f0d,
    .idProduct = 0x0092,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

// PS4-compatible Razer Panthera identity used by GP2040-CE. Console use still
// requires a licensed authentication response; descriptors alone cannot
// provide it.
static const tusb_desc_device_t kPs4DeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x1532,
    .idProduct = 0x0401,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

// Deliberately not TUD_HID_REPORT_DESC_GAMEPAD. That template declares six
// axes (X, Y, Z, Rz, Rx, Ry), and hosts do not index axes in descriptor
// order: SDL's Windows DirectInput backend sorts them into the fixed
// DIJOYSTATE2 order (X, Y, Z, Rx, Ry, Rz), so Rz landed on SDL axis 5 and the
// P2 pair Rx/Ry landed on axes 3 and 4. Taiko Arcade Loader reads P2 from
// rightx/righty, which the documented gamecontrollerdb entry binds to a2/a3,
// so P2's horizontal axis was read as its vertical one and P2's vertical axis
// was not read at all.
//
// Declaring only the four axes we drive removes the ambiguity: X, Y, Z, Rz
// occupy DIJOYSTATE2 offsets 0, 4, 8 and 20, so they enumerate as axes 0..3
// in declaration order under DirectInput, evdev, and plain report order
// alike. X/Y plus Z/Rz is also the conventional left-stick/right-stick pair
// for HID gamepads.
static const uint8_t kArcadeReportDescriptor[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(0x01)
        // P1 hit strength on X/Y, P2 hit strength on Z/Rz.
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
        HID_USAGE(HID_USAGE_DESKTOP_X),
        HID_USAGE(HID_USAGE_DESKTOP_Y),
        HID_USAGE(HID_USAGE_DESKTOP_Z),
        HID_USAGE(HID_USAGE_DESKTOP_RZ),
        HID_LOGICAL_MIN(0x81),
        HID_LOGICAL_MAX(0x7f),
        HID_REPORT_COUNT(4),
        HID_REPORT_SIZE(8),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        // 8 bit D-pad/hat.
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
        HID_USAGE(HID_USAGE_DESKTOP_HAT_SWITCH),
        HID_LOGICAL_MIN(1),
        HID_LOGICAL_MAX(8),
        HID_PHYSICAL_MIN(0),
        HID_PHYSICAL_MAX_N(315, 2),
        HID_REPORT_COUNT(1),
        HID_REPORT_SIZE(8),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        // 32 bit button map.
        HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
        HID_USAGE_MIN(1),
        HID_USAGE_MAX(32),
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX(1),
        HID_REPORT_COUNT(32),
        HID_REPORT_SIZE(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

#define ARCADE_CONFIG_LENGTH (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t kArcadeConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, USB_INTERFACE_COUNT, 0, ARCADE_CONFIG_LENGTH,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(USB_INTERFACE_GAMEPAD, 4, false,
                       sizeof(kArcadeReportDescriptor), 0x81,
                       sizeof(taiko_arcade_report_t), 1),
};

static const uint8_t kSwitchReportDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x15, 0x00, 0x25, 0x01,
    0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x10, 0x05, 0x09,
    0x19, 0x01, 0x29, 0x10, 0x81, 0x02, 0x05, 0x01, 0x25, 0x07,
    0x46, 0x3b, 0x01, 0x75, 0x04, 0x95, 0x01, 0x65, 0x14, 0x09,
    0x39, 0x81, 0x42, 0x65, 0x00, 0x95, 0x01, 0x81, 0x01, 0x26,
    0xff, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09,
    0x32, 0x09, 0x35, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0x06,
    0x00, 0xff, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02, 0x0a, 0x21,
    0x26, 0x95, 0x08, 0x91, 0x02, 0xc0,
};

#define SWITCH_CONFIG_LENGTH (9 + 9 + 9 + 7 + 7)
static const uint8_t kSwitchConfigurationDescriptor[] = {
    9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(SWITCH_CONFIG_LENGTH),
    1, 1, 0, 0x80, 250,
    9, TUSB_DESC_INTERFACE, 0, 0, 2, TUSB_CLASS_HID, 0, 0, 0,
    9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1,
    HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(sizeof(kSwitchReportDescriptor)),
    7, TUSB_DESC_ENDPOINT, 0x02, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(USB_ENDPOINT_SIZE), 1,
    7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(USB_ENDPOINT_SIZE), 1,
};

static const uint8_t kPs4ReportDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
    0x04, 0x81, 0x02,

    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00,
    0x46, 0x3b, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95,
    0x01, 0x81, 0x42,

    0x65, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0e,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0e,
    0x81, 0x02,

    0x06, 0x00, 0xff, 0x09, 0x20, 0x75, 0x06, 0x95,
    0x01, 0x81, 0x02,

    0x05, 0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81,
    0x02,

    0x06, 0x00, 0xff, 0x09, 0x21, 0x95, 0x36, 0x81,
    0x02,

    0x85, 0x05, 0x09, 0x22, 0x95, 0x1f, 0x91, 0x02,

    0x85, 0x03, 0x0a, 0x21, 0x27, 0x95, 0x2f, 0xb1,
    0x02,

    0x85, 0x02, 0x09, 0x24, 0x95, 0x24, 0xb1, 0x02,
    0x85, 0x08, 0x09, 0x25, 0x95, 0x03, 0xb1, 0x02,
    0x85, 0x10, 0x09, 0x26, 0x95, 0x04, 0xb1, 0x02,
    0x85, 0x11, 0x09, 0x27, 0x95, 0x02, 0xb1, 0x02,
    0x85, 0x12, 0x06, 0x02, 0xff, 0x09, 0x21, 0x95,
    0x0f, 0xb1, 0x02,
    0x85, 0x13, 0x09, 0x22, 0x95, 0x16, 0xb1, 0x02,
    0x85, 0x14, 0x06, 0x05, 0xff, 0x09, 0x20, 0x95,
    0x10, 0xb1, 0x02,
    0x85, 0x15, 0x09, 0x21, 0x95, 0x2c, 0xb1, 0x02,

    0x06, 0x80, 0xff,
    0x85, 0x80, 0x09, 0x20, 0x95, 0x06, 0xb1, 0x02,
    0x85, 0x81, 0x09, 0x21, 0x95, 0x06, 0xb1, 0x02,
    0x85, 0x82, 0x09, 0x22, 0x95, 0x05, 0xb1, 0x02,
    0x85, 0x83, 0x09, 0x23, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0x84, 0x09, 0x24, 0x95, 0x04, 0xb1, 0x02,
    0x85, 0x85, 0x09, 0x25, 0x95, 0x06, 0xb1, 0x02,
    0x85, 0x86, 0x09, 0x26, 0x95, 0x06, 0xb1, 0x02,
    0x85, 0x87, 0x09, 0x27, 0x95, 0x23, 0xb1, 0x02,
    0x85, 0x88, 0x09, 0x28, 0x95, 0x22, 0xb1, 0x02,
    0x85, 0x89, 0x09, 0x29, 0x95, 0x02, 0xb1, 0x02,
    0x85, 0x90, 0x09, 0x30, 0x95, 0x05, 0xb1, 0x02,
    0x85, 0x91, 0x09, 0x31, 0x95, 0x03, 0xb1, 0x02,
    0x85, 0x92, 0x09, 0x32, 0x95, 0x03, 0xb1, 0x02,
    0x85, 0x93, 0x09, 0x33, 0x95, 0x0c, 0xb1, 0x02,
    0x85, 0xa0, 0x09, 0x40, 0x95, 0x06, 0xb1, 0x02,
    0x85, 0xa1, 0x09, 0x41, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xa2, 0x09, 0x42, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xa3, 0x09, 0x43, 0x95, 0x30, 0xb1, 0x02,
    0x85, 0xa4, 0x09, 0x44, 0x95, 0x0d, 0xb1, 0x02,
    0x85, 0xa5, 0x09, 0x45, 0x95, 0x15, 0xb1, 0x02,
    0x85, 0xa6, 0x09, 0x46, 0x95, 0x15, 0xb1, 0x02,
    0x85, 0xa7, 0x09, 0x4a, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xa8, 0x09, 0x4b, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xa9, 0x09, 0x4c, 0x95, 0x08, 0xb1, 0x02,
    0x85, 0xaa, 0x09, 0x4e, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xab, 0x09, 0x4f, 0x95, 0x39, 0xb1, 0x02,
    0x85, 0xac, 0x09, 0x50, 0x95, 0x39, 0xb1, 0x02,
    0x85, 0xad, 0x09, 0x51, 0x95, 0x0b, 0xb1, 0x02,
    0x85, 0xae, 0x09, 0x52, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xaf, 0x09, 0x53, 0x95, 0x02, 0xb1, 0x02,
    0x85, 0xb0, 0x09, 0x54, 0x95, 0x3f, 0xb1, 0x02,
    0xc0,

    0x06, 0xf0, 0xff, 0x09, 0x40, 0xa1, 0x01,
    0x85, 0xf0, 0x09, 0x47, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf1, 0x09, 0x48, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf2, 0x09, 0x49, 0x95, 0x0f, 0xb1, 0x02,
    0x85, 0xf3, 0x0a, 0x01, 0x47, 0x95, 0x07, 0xb1,
    0x02, 0xc0,
};

#define PS4_CONFIG_LENGTH (9 + 9 + 9 + 7 + 7)
static const uint8_t kPs4ConfigurationDescriptor[] = {
    9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(PS4_CONFIG_LENGTH),
    1, 1, 0, 0x80, 50,
    9, TUSB_DESC_INTERFACE, 0, 0, 2, TUSB_CLASS_HID, 0, 0, 0,
    9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1,
    HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(sizeof(kPs4ReportDescriptor)),
    7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(USB_ENDPOINT_SIZE), 1,
    7, TUSB_DESC_ENDPOINT, 0x03, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(USB_ENDPOINT_SIZE), 1,
};

#define XINPUT_CONFIG_LENGTH (TUD_CONFIG_DESC_LEN + XINPUT_INTERFACE_DESCRIPTOR_LENGTH)
static const uint8_t kXinputConfigurationDescriptor[] = {
    9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(XINPUT_CONFIG_LENGTH),
    1, 1, 0, (0x80 | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP), 50,

    9, TUSB_DESC_INTERFACE, 0, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC,
    XINPUT_INTERFACE_SUBCLASS, XINPUT_INTERFACE_PROTOCOL, 4,

    16, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0110), 0x01, 0x24,
    XINPUT_IN_ENDPOINT, sizeof(taiko_xinput_report_t),
    0x03, 0x00, 0x03, 0x13, 0x01, 0x00, 0x03, 0x00,

    7, TUSB_DESC_ENDPOINT, XINPUT_IN_ENDPOINT, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(XINPUT_ENDPOINT_SIZE), 1,
    7, TUSB_DESC_ENDPOINT, XINPUT_OUT_ENDPOINT, TUSB_XFER_INTERRUPT,
    U16_TO_U8S_LE(XINPUT_ENDPOINT_SIZE), 8,
};

static const uint8_t kBosDescriptor[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LENGTH, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESCRIPTOR_LENGTH,
                                MICROSOFT_VENDOR_REQUEST),
};

static const uint8_t kMicrosoftOs20Descriptor[MS_OS_20_DESCRIPTOR_LENGTH] = {
    U16_TO_U8S_LE(0x000a), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESCRIPTOR_LENGTH),

    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0, U16_TO_U8S_LE(MS_OS_20_DESCRIPTOR_LENGTH - 0x0a),

    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    0, 0, U16_TO_U8S_LE(MS_OS_20_DESCRIPTOR_LENGTH - 0x0a - 0x08),

    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'X', 'U', 'S', 'B', '2', '0', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    U16_TO_U8S_LE(MS_OS_20_DESCRIPTOR_LENGTH - 0x0a - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002a),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0,
    'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0,
    'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(0x0050),
    '{', 0, '8', 0, 'D', 0, '9', 0, '0', 0, '8', 0,
    '4', 0, '2', 0, 'C', 0, '-', 0, '1', 0, '5', 0,
    '9', 0, '4', 0, '-', 0, '4', 0, '1', 0, 'C', 0,
    'E', 0, '-', 0, 'A', 0, 'A', 0, '3', 0, 'F', 0,
    '-', 0, '6', 0, '2', 0, 'D', 0, '4', 0, '6', 0,
    '4', 0, 'E', 0, '1', 0, 'B', 0, 'E', 0, '7', 0,
    '9', 0, '}', 0, 0, 0, 0, 0,
};

_Static_assert(sizeof(kArcadeConfigurationDescriptor) ==
                   ARCADE_CONFIG_LENGTH,
               "Arcade configuration descriptor length changed");
_Static_assert(sizeof(kXinputConfigurationDescriptor) ==
                   XINPUT_CONFIG_LENGTH,
               "XInput configuration descriptor length changed");
_Static_assert(sizeof(kSwitchConfigurationDescriptor) ==
                   SWITCH_CONFIG_LENGTH,
               "Switch configuration descriptor length changed");
_Static_assert(sizeof(kPs4ConfigurationDescriptor) == PS4_CONFIG_LENGTH,
               "PS4 configuration descriptor length changed");
_Static_assert(sizeof(kArcadeReportDescriptor) == 64,
               "Arcade report descriptor length changed");
_Static_assert(sizeof(kSwitchReportDescriptor) == 86,
               "Switch report descriptor length changed");
_Static_assert(sizeof(kPs4ReportDescriptor) == 481,
               "PS4 report descriptor length changed");
_Static_assert(sizeof(kBosDescriptor) == BOS_TOTAL_LENGTH,
               "BOS descriptor length changed");

static const char *kArcadeStrings[] = {
    kLanguage,
    "Taiko Community",
    "Taiko Controller",
    "0001",
    "Gamepad",
};

static const char *kXinputStrings[] = {
    kLanguage,
    "Taiko Community",
    "Taiko Controller (XInput)",
    "0001",
    "XInput gamepad",
};

static const char *kSwitchStrings[] = {
    kLanguage,
    "HORI CO.,LTD.",
    "POKKEN CONTROLLER",
};

static const char *kPs4Strings[] = {
    kLanguage,
    "Taiko Community",
    "Taiko Controller (PS4)",
};

static const uint8_t kPs4Calibration[] = {
    0xfe, 0xff, 0x0e, 0x00, 0x04, 0x00, 0xd4, 0x22,
    0x2a, 0xdd, 0xbb, 0x22, 0x5e, 0xdd, 0x81, 0x22,
    0x84, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0x85, 0x1f,
    0xb0, 0xe0, 0xc6, 0x20, 0xb5, 0xe0, 0xb1, 0x20,
    0x83, 0xdf, 0x0c, 0x00,
};

static const uint8_t kPs4ControllerDefinition[] = {
    0x21, 0x27, 0x04, 0xcf, 0x00, 0x2c, 0x56, 0x08,
    0x00, 0x3d, 0x00, 0xe8, 0x03, 0x04, 0x00, 0xff,
    0x7f, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x0d,
    0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t kPs4MacAddress[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x25, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t kPs4Version[] = {
    0x4a, 0x75, 0x6e, 0x20, 0x20, 0x39, 0x20, 0x32,
    0x30, 0x31, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0x32, 0x3a, 0x33, 0x36, 0x3a, 0x34, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0xb4, 0x01, 0x00, 0x00, 0x00,
    0x07, 0xa0, 0x10, 0x20, 0x00, 0xa0, 0x02, 0x00,
};

static const uint8_t kPs4AuthReset[] = {
    0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00,
};

static uint16_t copy_limited(uint8_t *destination, uint16_t destination_size,
                             const uint8_t *source, size_t source_size) {
    const uint16_t count =
        source_size < destination_size ? (uint16_t)source_size : destination_size;
    memcpy(destination, source, count);
    return count;
}

static taiko_input_snapshot_t last_input_snapshot(void) {
    taiko_input_snapshot_t input;
    portENTER_CRITICAL(&s_last_input_lock);
    input = s_last_input;
    portEXIT_CRITICAL(&s_last_input_lock);
    return input;
}

static uint32_t ps4_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

static void xinput_init(void) {
    s_xinput_in_endpoint = 0;
    s_xinput_out_endpoint = 0;
    memset(s_xinput_out_buffer, 0, sizeof(s_xinput_out_buffer));
}

static bool xinput_deinit(void) {
    xinput_init();
    return true;
}

static void xinput_reset(uint8_t rhport) {
    (void)rhport;
    xinput_init();
}

static uint16_t xinput_open(uint8_t rhport,
                            const tusb_desc_interface_t *interface_descriptor,
                            uint16_t max_length) {
    if (s_mode != TAIKO_CONTROLLER_MODE_PC ||
        interface_descriptor->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        interface_descriptor->bInterfaceSubClass != XINPUT_INTERFACE_SUBCLASS ||
        interface_descriptor->bInterfaceProtocol != XINPUT_INTERFACE_PROTOCOL) {
        return 0;
    }

    const uint8_t *descriptor = (const uint8_t *)interface_descriptor;
    uint16_t consumed = interface_descriptor->bLength;
    descriptor += interface_descriptor->bLength;
    uint8_t endpoint_count = 0;

    while (consumed < max_length &&
           endpoint_count < interface_descriptor->bNumEndpoints) {
        const uint8_t descriptor_length = descriptor[0];
        if (descriptor_length < 2 ||
            descriptor_length > (uint16_t)(max_length - consumed)) {
            return 0;
        }
        if (descriptor[1] == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t *endpoint =
                (const tusb_desc_endpoint_t *)descriptor;
            if (!usbd_edpt_open(rhport, endpoint)) {
                return 0;
            }
            if (tu_edpt_dir(endpoint->bEndpointAddress) == TUSB_DIR_IN) {
                s_xinput_in_endpoint = endpoint->bEndpointAddress;
            } else {
                s_xinput_out_endpoint = endpoint->bEndpointAddress;
            }
            endpoint_count++;
        }
        descriptor += descriptor_length;
        consumed += descriptor_length;
    }

    if (endpoint_count != interface_descriptor->bNumEndpoints ||
        s_xinput_in_endpoint == 0 || s_xinput_out_endpoint == 0) {
        return 0;
    }
    return consumed;
}

static bool xinput_control_xfer(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request) {
    (void)rhport;
    (void)stage;
    (void)request;
    return true;
}

static bool arm_xinput_out_endpoint(void) {
    if (s_xinput_out_endpoint == 0 ||
        usbd_edpt_busy(0, s_xinput_out_endpoint) ||
        !usbd_edpt_claim(0, s_xinput_out_endpoint)) {
        return false;
    }
    const bool queued = usbd_edpt_xfer(0, s_xinput_out_endpoint,
                                       s_xinput_out_buffer,
                                       sizeof(s_xinput_out_buffer));
    (void)usbd_edpt_release(0, s_xinput_out_endpoint);
    return queued;
}

static bool xinput_transfer_complete(uint8_t rhport, uint8_t endpoint_address,
                                     xfer_result_t result,
                                     uint32_t transferred_bytes) {
    (void)rhport;
    (void)result;
    (void)transferred_bytes;
    if (endpoint_address == s_xinput_out_endpoint) {
        (void)arm_xinput_out_endpoint();
    }
    return true;
}

static const usbd_class_driver_t kXinputClassDriver = {
    .name = "XINPUT",
    .init = xinput_init,
    .deinit = xinput_deinit,
    .reset = xinput_reset,
    .open = xinput_open,
    .control_xfer_cb = xinput_control_xfer,
    .xfer_cb = xinput_transfer_complete,
    .xfer_isr = NULL,
    .sof = NULL,
};

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &kXinputClassDriver;
}

const uint8_t *tud_descriptor_bos_cb(void) {
    return s_mode == TAIKO_CONTROLLER_MODE_PC ? kBosDescriptor : NULL;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request) {
    if (s_mode != TAIKO_CONTROLLER_MODE_PC) {
        return false;
    }
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == MICROSOFT_VENDOR_REQUEST &&
        request->wIndex == 0x0007) {
        return tud_control_xfer(rhport, request,
                                (void *)kMicrosoftOs20Descriptor,
                                sizeof(kMicrosoftOs20Descriptor));
    }
    return false;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    switch (s_mode) {
        case TAIKO_CONTROLLER_MODE_ARCADE:
            return kArcadeReportDescriptor;
        case TAIKO_CONTROLLER_MODE_SWITCH:
            return kSwitchReportDescriptor;
        case TAIKO_CONTROLLER_MODE_PS4:
            return kPs4ReportDescriptor;
        case TAIKO_CONTROLLER_MODE_PC:
            return NULL;
    }
    return NULL;
}

static uint16_t ps4_get_feature_report(uint8_t report_id, uint8_t *buffer,
                                       uint16_t requested_length) {
    switch (report_id) {
        case 0x02:
            return copy_limited(buffer, requested_length, kPs4Calibration,
                                sizeof(kPs4Calibration));
        case 0x03:
            return copy_limited(buffer, requested_length,
                                kPs4ControllerDefinition,
                                sizeof(kPs4ControllerDefinition));
        case 0x12:
            return copy_limited(buffer, requested_length, kPs4MacAddress,
                                sizeof(kPs4MacAddress));
        case 0xa3:
            return copy_limited(buffer, requested_length, kPs4Version,
                                sizeof(kPs4Version));
        case 0xf1: {
            if (taiko_ps4_auth_available()) {
                return (uint16_t)taiko_ps4_auth_get_signature(
                    buffer, requested_length);
            }
            // There is deliberately no fabricated signature. This correctly
            // shaped zero response is useful for USB development, but cannot
            // satisfy a PS4 authentication challenge.
            uint8_t response[64] = {
                0xf1,
                __atomic_load_n(&s_ps4_nonce_id, __ATOMIC_RELAXED),
            };
            const uint32_t crc = ps4_crc32(response, 60);
            memcpy(&response[60], &crc, sizeof(crc));
            return copy_limited(buffer, requested_length, &response[1], 63);
        }
        case 0xf2: {
            if (taiko_ps4_auth_available()) {
                return (uint16_t)taiko_ps4_auth_get_status(
                    buffer, requested_length);
            }
            uint8_t response[16] = {
                0xf2,
                __atomic_load_n(&s_ps4_nonce_id, __ATOMIC_RELAXED),
                0x10,
            };
            const uint32_t crc = ps4_crc32(response, 12);
            memcpy(&response[12], &crc, sizeof(crc));
            return copy_limited(buffer, requested_length, &response[1], 15);
        }
        case 0xf3:
            taiko_ps4_auth_reset();
            return copy_limited(buffer, requested_length, kPs4AuthReset,
                                sizeof(kPs4AuthReset));
        default:
            if (requested_length == 0) {
                return 0;
            }
            memset(buffer, 0, requested_length);
            return requested_length;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t requested_length) {
    (void)instance;
    if (s_mode == TAIKO_CONTROLLER_MODE_PS4 &&
        report_type == HID_REPORT_TYPE_FEATURE) {
        return ps4_get_feature_report(report_id, buffer, requested_length);
    }

    if (report_type != HID_REPORT_TYPE_INPUT) {
        return 0;
    }
    const taiko_input_snapshot_t input = last_input_snapshot();
    switch (s_mode) {
        case TAIKO_CONTROLLER_MODE_ARCADE: {
            taiko_arcade_report_t report;
            taiko_build_arcade_report(&input, &report);
            return copy_limited(buffer, requested_length,
                                (const uint8_t *)&report, sizeof(report));
        }
        case TAIKO_CONTROLLER_MODE_SWITCH: {
            taiko_switch_report_t report;
            taiko_build_switch_report(&input, &report);
            return copy_limited(buffer, requested_length,
                                (const uint8_t *)&report, sizeof(report));
        }
        case TAIKO_CONTROLLER_MODE_PS4: {
            taiko_ps4_report_t report;
            taiko_build_ps4_report(
                &input,
                __atomic_load_n(&s_ps4_report_counter, __ATOMIC_RELAXED),
                &report);
            return copy_limited(buffer, requested_length,
                                ((const uint8_t *)&report) + 1,
                                sizeof(report) - 1);
        }
        case TAIKO_CONTROLLER_MODE_PC:
            return 0;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t buffer_size) {
    (void)instance;
    if (s_mode == TAIKO_CONTROLLER_MODE_PS4 &&
        report_type == HID_REPORT_TYPE_FEATURE && report_id == 0xf0 &&
        buffer_size >= 1) {
        __atomic_store_n(&s_ps4_nonce_id, buffer[0], __ATOMIC_RELAXED);
        if (taiko_ps4_auth_available()) {
            (void)taiko_ps4_auth_set_nonce(buffer, buffer_size);
        }
    }
    // Rumble, light-bar, and player-LED outputs are accepted by the endpoints
    // but intentionally do not drive the per-hit LED chain.
}

static bool send_xinput_report(const taiko_input_snapshot_t *input) {
    (void)arm_xinput_out_endpoint();
    if (s_xinput_in_endpoint == 0 ||
        usbd_edpt_busy(0, s_xinput_in_endpoint) ||
        !usbd_edpt_claim(0, s_xinput_in_endpoint)) {
        return false;
    }

    taiko_build_xinput_report(input, &s_xinput_report);
    const bool queued = usbd_edpt_xfer(0, s_xinput_in_endpoint,
                                       (uint8_t *)&s_xinput_report,
                                       sizeof(s_xinput_report));
    (void)usbd_edpt_release(0, s_xinput_in_endpoint);
    return queued;
}

esp_err_t taiko_usb_install(taiko_controller_mode_t mode) {
    const tusb_desc_device_t *device_descriptor = NULL;
    const uint8_t *configuration_descriptor = NULL;
    const char **strings = NULL;
    int string_count = 0;

    s_mode = mode;
    memset(&s_last_input, 0, sizeof(s_last_input));
    memset(&s_arcade_report, 0, sizeof(s_arcade_report));
    memset(&s_xinput_report, 0, sizeof(s_xinput_report));
    memset(&s_switch_report, 0, sizeof(s_switch_report));
    memset(&s_ps4_report, 0, sizeof(s_ps4_report));
    __atomic_store_n(&s_ps4_report_counter, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_ps4_nonce_id, 0, __ATOMIC_RELAXED);

    if (mode == TAIKO_CONTROLLER_MODE_PS4) {
        const esp_err_t auth_result = taiko_ps4_auth_init();
        if (auth_result != ESP_OK && auth_result != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "PS4 authentication initialization failed: %s",
                     esp_err_to_name(auth_result));
        }
    }

    switch (mode) {
        case TAIKO_CONTROLLER_MODE_ARCADE:
            device_descriptor = &kArcadeDeviceDescriptor;
            configuration_descriptor = kArcadeConfigurationDescriptor;
            strings = kArcadeStrings;
            string_count = sizeof(kArcadeStrings) / sizeof(kArcadeStrings[0]);
            break;
        case TAIKO_CONTROLLER_MODE_PC:
            device_descriptor = &kXinputDeviceDescriptor;
            configuration_descriptor = kXinputConfigurationDescriptor;
            strings = kXinputStrings;
            string_count = sizeof(kXinputStrings) / sizeof(kXinputStrings[0]);
            break;
        case TAIKO_CONTROLLER_MODE_SWITCH:
            device_descriptor = &kSwitchDeviceDescriptor;
            configuration_descriptor = kSwitchConfigurationDescriptor;
            strings = kSwitchStrings;
            string_count = sizeof(kSwitchStrings) / sizeof(kSwitchStrings[0]);
            break;
        case TAIKO_CONTROLLER_MODE_PS4:
            device_descriptor = &kPs4DeviceDescriptor;
            configuration_descriptor = kPs4ConfigurationDescriptor;
            strings = kPs4Strings;
            string_count = sizeof(kPs4Strings) / sizeof(kPs4Strings[0]);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    tinyusb_config_t tinyusb_config = TINYUSB_DEFAULT_CONFIG();
    tinyusb_config.descriptor.device = device_descriptor;
    tinyusb_config.descriptor.full_speed_config = configuration_descriptor;
    tinyusb_config.descriptor.string = strings;
    tinyusb_config.descriptor.string_count = string_count;
    return tinyusb_driver_install(&tinyusb_config);
}

bool taiko_usb_send(const taiko_input_snapshot_t *input) {
    if (input == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_last_input_lock);
    s_last_input = *input;
    portEXIT_CRITICAL(&s_last_input_lock);

    if (!tud_mounted()) {
        return false;
    }
    if (tud_suspended()) {
        if (taiko_input_has_activity(input, s_mode)) {
            (void)tud_remote_wakeup();
        }
        return false;
    }

    switch (s_mode) {
        case TAIKO_CONTROLLER_MODE_ARCADE:
            if (!tud_hid_ready()) {
                return false;
            }
            taiko_build_arcade_report(input, &s_arcade_report);
            return tud_hid_report(0x01, &s_arcade_report,
                                  sizeof(s_arcade_report));
        case TAIKO_CONTROLLER_MODE_PC:
            return send_xinput_report(input);
        case TAIKO_CONTROLLER_MODE_SWITCH:
            if (!tud_hid_ready()) {
                return false;
            }
            taiko_build_switch_report(input, &s_switch_report);
            return tud_hid_report(0, &s_switch_report,
                                  sizeof(s_switch_report));
        case TAIKO_CONTROLLER_MODE_PS4:
            if (!tud_hid_ready()) {
                return false;
            }
            const uint8_t report_counter = __atomic_load_n(
                &s_ps4_report_counter, __ATOMIC_RELAXED);
            taiko_build_ps4_report(input, report_counter, &s_ps4_report);
            if (tud_hid_report(0, &s_ps4_report, sizeof(s_ps4_report))) {
                __atomic_store_n(
                    &s_ps4_report_counter,
                    (uint8_t)((report_counter + 1U) & 0x3fU),
                    __ATOMIC_RELAXED);
                return true;
            }
            return false;
    }
    return false;
}

bool taiko_usb_ps4_authentication_available(void) {
    return taiko_ps4_auth_available();
}
