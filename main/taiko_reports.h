#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TAIKO_CONTROLLER_MODE_ARCADE = 0,
    TAIKO_CONTROLLER_MODE_PC,
    TAIKO_CONTROLLER_MODE_SWITCH,
    TAIKO_CONTROLLER_MODE_PS4,
} taiko_controller_mode_t;

enum {
    TAIKO_DPAD_UP = 1U << 0,
    TAIKO_DPAD_RIGHT = 1U << 1,
    TAIKO_DPAD_DOWN = 1U << 2,
    TAIKO_DPAD_LEFT = 1U << 3,
};

enum {
    TAIKO_BUTTON_FACE_UP = 1U << 0,
    TAIKO_BUTTON_FACE_RIGHT = 1U << 1,
    TAIKO_BUTTON_FACE_DOWN = 1U << 2,
    TAIKO_BUTTON_FACE_LEFT = 1U << 3,
    TAIKO_BUTTON_L1 = 1U << 4,
    TAIKO_BUTTON_R1 = 1U << 5,
    TAIKO_BUTTON_L2 = 1U << 6,
    TAIKO_BUTTON_R2 = 1U << 7,
    TAIKO_BUTTON_SELECT = 1U << 8,
    TAIKO_BUTTON_START = 1U << 9,
    TAIKO_BUTTON_HOME = 1U << 10,
};

enum {
    TAIKO_DRUM_LEFT_DON = 1U << 0,
    TAIKO_DRUM_RIGHT_DON = 1U << 1,
    TAIKO_DRUM_LEFT_KA = 1U << 2,
    TAIKO_DRUM_RIGHT_KA = 1U << 3,
};

// The canonical hat values intentionally match TinyUSB's generic gamepad
// values. Switch and PS4 use a different on-wire neutral/direction encoding;
// their builders translate it explicitly.
typedef enum {
    TAIKO_HAT_CENTERED = 0,
    TAIKO_HAT_UP = 1,
    TAIKO_HAT_UP_RIGHT = 2,
    TAIKO_HAT_RIGHT = 3,
    TAIKO_HAT_DOWN_RIGHT = 4,
    TAIKO_HAT_DOWN = 5,
    TAIKO_HAT_DOWN_LEFT = 6,
    TAIKO_HAT_LEFT = 7,
    TAIKO_HAT_UP_LEFT = 8,
} taiko_hat_t;

typedef struct {
    uint8_t dpad;
    uint16_t buttons;
    uint8_t drum_buttons;
    int8_t arcade_p1_x;
    int8_t arcade_p1_y;
    int8_t arcade_p2_x;
    int8_t arcade_p2_y;
} taiko_input_snapshot_t;

// Matches kArcadeReportDescriptor: P1 on X/Y, P2 on Z/Rz. Declaring exactly
// these four axes is what makes them enumerate as host axes 0..3, which is
// the order Taiko Arcade Loader's leftx/lefty/rightx/righty bindings expect.
typedef struct __attribute__((packed)) {
    int8_t x;
    int8_t y;
    int8_t z;
    int8_t rz;
    uint8_t hat;
    uint32_t buttons;
} taiko_arcade_report_t;

typedef struct __attribute__((packed)) {
    uint8_t report_id;
    uint8_t report_size;
    uint8_t buttons1;
    uint8_t buttons2;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint8_t reserved[6];
} taiko_xinput_report_t;

typedef struct __attribute__((packed)) {
    uint16_t buttons;
    uint8_t hat;
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t vendor;
} taiko_switch_report_t;

typedef struct __attribute__((packed)) {
    uint8_t report_id;
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t buttons1;
    uint8_t buttons2;
    uint8_t buttons3;
    uint8_t left_trigger;
    uint8_t right_trigger;
    uint8_t vendor[54];
} taiko_ps4_report_t;

taiko_hat_t taiko_resolve_hat(uint8_t dpad);

void taiko_build_arcade_report(
    const taiko_input_snapshot_t *input, taiko_arcade_report_t *report);
void taiko_build_xinput_report(
    const taiko_input_snapshot_t *input, taiko_xinput_report_t *report);
void taiko_build_switch_report(
    const taiko_input_snapshot_t *input, taiko_switch_report_t *report);
void taiko_build_ps4_report(const taiko_input_snapshot_t *input,
                            uint8_t report_counter,
                            taiko_ps4_report_t *report);

bool taiko_input_has_activity(const taiko_input_snapshot_t *input,
                              taiko_controller_mode_t mode);
