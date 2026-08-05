#include "taiko_reports.h"

#include <string.h>

_Static_assert(sizeof(taiko_arcade_report_t) == 11,
               "Arcade HID report layout changed");
_Static_assert(sizeof(taiko_xinput_report_t) == 20,
               "XInput report layout changed");
_Static_assert(sizeof(taiko_switch_report_t) == 8,
               "Switch report layout changed");
_Static_assert(sizeof(taiko_ps4_report_t) == 64,
               "PS4 report layout changed");
_Static_assert(TAIKO_CONTROLLER_MODE_ARCADE == 0 &&
                   TAIKO_CONTROLLER_MODE_PC == 1 &&
                   TAIKO_CONTROLLER_MODE_SWITCH == 2 &&
                   TAIKO_CONTROLLER_MODE_PS4 == 3,
               "Controller mode values must match DIP4:DIP3");

enum {
    XINPUT_DPAD_UP = 1U << 0,
    XINPUT_DPAD_DOWN = 1U << 1,
    XINPUT_DPAD_LEFT = 1U << 2,
    XINPUT_DPAD_RIGHT = 1U << 3,
    XINPUT_START = 1U << 4,
    XINPUT_BACK = 1U << 5,
};

enum {
    XINPUT_LB = 1U << 0,
    XINPUT_RB = 1U << 1,
    XINPUT_HOME = 1U << 2,
    XINPUT_A = 1U << 4,
    XINPUT_B = 1U << 5,
    XINPUT_X = 1U << 6,
    XINPUT_Y = 1U << 7,
};

enum {
    SWITCH_Y = 1U << 0,
    SWITCH_B = 1U << 1,
    SWITCH_A = 1U << 2,
    SWITCH_X = 1U << 3,
    SWITCH_L = 1U << 4,
    SWITCH_R = 1U << 5,
    SWITCH_ZL = 1U << 6,
    SWITCH_ZR = 1U << 7,
    SWITCH_MINUS = 1U << 8,
    SWITCH_PLUS = 1U << 9,
    SWITCH_HOME = 1U << 12,
};

enum {
    PS4_SQUARE = 1U << 4,
    PS4_CROSS = 1U << 5,
    PS4_CIRCLE = 1U << 6,
    PS4_TRIANGLE = 1U << 7,
};

enum {
    PS4_L1 = 1U << 0,
    PS4_R1 = 1U << 1,
    PS4_L2 = 1U << 2,
    PS4_R2 = 1U << 3,
    PS4_SHARE = 1U << 4,
    PS4_OPTIONS = 1U << 5,
};

#define PS4_HOME (1U << 0)

typedef struct {
    uint8_t dpad;
    uint16_t buttons;
} digital_controls_t;

static digital_controls_t digital_controls(
    const taiko_input_snapshot_t *input) {
    digital_controls_t controls = {
        .dpad = input->dpad,
        .buttons = input->buttons,
    };

    if ((input->drum_buttons & TAIKO_DRUM_LEFT_DON) != 0) {
        controls.dpad |= TAIKO_DPAD_DOWN;
    }
    if ((input->drum_buttons & TAIKO_DRUM_RIGHT_DON) != 0) {
        controls.buttons |= TAIKO_BUTTON_FACE_DOWN;
    }
    if ((input->drum_buttons & TAIKO_DRUM_LEFT_KA) != 0) {
        controls.buttons |= TAIKO_BUTTON_L1;
    }
    if ((input->drum_buttons & TAIKO_DRUM_RIGHT_KA) != 0) {
        controls.buttons |= TAIKO_BUTTON_R1;
    }
    return controls;
}

static uint8_t cleaned_dpad(uint8_t dpad) {
    if ((dpad & (TAIKO_DPAD_UP | TAIKO_DPAD_DOWN)) ==
        (TAIKO_DPAD_UP | TAIKO_DPAD_DOWN)) {
        dpad &= (uint8_t)~(TAIKO_DPAD_UP | TAIKO_DPAD_DOWN);
    }
    if ((dpad & (TAIKO_DPAD_LEFT | TAIKO_DPAD_RIGHT)) ==
        (TAIKO_DPAD_LEFT | TAIKO_DPAD_RIGHT)) {
        dpad &= (uint8_t)~(TAIKO_DPAD_LEFT | TAIKO_DPAD_RIGHT);
    }
    return dpad;
}

taiko_hat_t taiko_resolve_hat(uint8_t dpad) {
    dpad = cleaned_dpad(dpad);
    switch (dpad) {
        case TAIKO_DPAD_UP:
            return TAIKO_HAT_UP;
        case TAIKO_DPAD_UP | TAIKO_DPAD_RIGHT:
            return TAIKO_HAT_UP_RIGHT;
        case TAIKO_DPAD_RIGHT:
            return TAIKO_HAT_RIGHT;
        case TAIKO_DPAD_DOWN | TAIKO_DPAD_RIGHT:
            return TAIKO_HAT_DOWN_RIGHT;
        case TAIKO_DPAD_DOWN:
            return TAIKO_HAT_DOWN;
        case TAIKO_DPAD_DOWN | TAIKO_DPAD_LEFT:
            return TAIKO_HAT_DOWN_LEFT;
        case TAIKO_DPAD_LEFT:
            return TAIKO_HAT_LEFT;
        case TAIKO_DPAD_UP | TAIKO_DPAD_LEFT:
            return TAIKO_HAT_UP_LEFT;
        default:
            return TAIKO_HAT_CENTERED;
    }
}

static uint32_t arcade_button_bitmap(uint16_t buttons) {
    uint32_t bitmap = 0;
    if ((buttons & TAIKO_BUTTON_FACE_DOWN) != 0) {
        bitmap |= 1U << 0;  // South
    }
    if ((buttons & TAIKO_BUTTON_FACE_RIGHT) != 0) {
        bitmap |= 1U << 1;  // East
    }
    if ((buttons & TAIKO_BUTTON_FACE_UP) != 0) {
        bitmap |= 1U << 3;  // North
    }
    if ((buttons & TAIKO_BUTTON_FACE_LEFT) != 0) {
        bitmap |= 1U << 4;  // West
    }
    if ((buttons & TAIKO_BUTTON_L1) != 0) {
        bitmap |= 1U << 6;
    }
    if ((buttons & TAIKO_BUTTON_R1) != 0) {
        bitmap |= 1U << 7;
    }
    if ((buttons & TAIKO_BUTTON_L2) != 0) {
        bitmap |= 1U << 8;
    }
    if ((buttons & TAIKO_BUTTON_R2) != 0) {
        bitmap |= 1U << 9;
    }
    if ((buttons & TAIKO_BUTTON_SELECT) != 0) {
        bitmap |= 1U << 10;
    }
    if ((buttons & TAIKO_BUTTON_START) != 0) {
        bitmap |= 1U << 11;
    }
    if ((buttons & TAIKO_BUTTON_HOME) != 0) {
        bitmap |= 1U << 12;
    }
    return bitmap;
}

void taiko_build_arcade_report(
    const taiko_input_snapshot_t *input, taiko_arcade_report_t *report) {
    *report = (taiko_arcade_report_t){
        .x = input->arcade_x,
        .y = input->arcade_y,
        .rx = input->arcade_rx,
        .ry = input->arcade_ry,
        .hat = (uint8_t)taiko_resolve_hat(input->dpad),
        .buttons = arcade_button_bitmap(input->buttons),
    };
}

void taiko_build_xinput_report(
    const taiko_input_snapshot_t *input, taiko_xinput_report_t *report) {
    const digital_controls_t controls = digital_controls(input);
    const uint8_t dpad = cleaned_dpad(controls.dpad);
    *report = (taiko_xinput_report_t){
        .report_size = sizeof(*report),
    };

    if ((dpad & TAIKO_DPAD_UP) != 0) {
        report->buttons1 |= XINPUT_DPAD_UP;
    }
    if ((dpad & TAIKO_DPAD_DOWN) != 0) {
        report->buttons1 |= XINPUT_DPAD_DOWN;
    }
    if ((dpad & TAIKO_DPAD_LEFT) != 0) {
        report->buttons1 |= XINPUT_DPAD_LEFT;
    }
    if ((dpad & TAIKO_DPAD_RIGHT) != 0) {
        report->buttons1 |= XINPUT_DPAD_RIGHT;
    }
    if ((controls.buttons & TAIKO_BUTTON_START) != 0) {
        report->buttons1 |= XINPUT_START;
    }
    if ((controls.buttons & TAIKO_BUTTON_SELECT) != 0) {
        report->buttons1 |= XINPUT_BACK;
    }
    if ((controls.buttons & TAIKO_BUTTON_L1) != 0) {
        report->buttons2 |= XINPUT_LB;
    }
    if ((controls.buttons & TAIKO_BUTTON_R1) != 0) {
        report->buttons2 |= XINPUT_RB;
    }
    if ((controls.buttons & TAIKO_BUTTON_HOME) != 0) {
        report->buttons2 |= XINPUT_HOME;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_DOWN) != 0) {
        report->buttons2 |= XINPUT_A;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_RIGHT) != 0) {
        report->buttons2 |= XINPUT_B;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_LEFT) != 0) {
        report->buttons2 |= XINPUT_X;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_UP) != 0) {
        report->buttons2 |= XINPUT_Y;
    }
    if ((controls.buttons & TAIKO_BUTTON_L2) != 0) {
        report->left_trigger = UINT8_MAX;
    }
    if ((controls.buttons & TAIKO_BUTTON_R2) != 0) {
        report->right_trigger = UINT8_MAX;
    }
}

static uint8_t console_hat(taiko_hat_t hat) {
    return hat == TAIKO_HAT_CENTERED ? 8U : (uint8_t)hat - 1U;
}

void taiko_build_switch_report(
    const taiko_input_snapshot_t *input, taiko_switch_report_t *report) {
    const digital_controls_t controls = digital_controls(input);
    *report = (taiko_switch_report_t){
        .hat = console_hat(taiko_resolve_hat(controls.dpad)),
        .left_x = 0x80,
        .left_y = 0x80,
        .right_x = 0x80,
        .right_y = 0x80,
    };

    if ((controls.buttons & TAIKO_BUTTON_FACE_LEFT) != 0) {
        report->buttons |= SWITCH_Y;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_DOWN) != 0) {
        report->buttons |= SWITCH_B;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_RIGHT) != 0) {
        report->buttons |= SWITCH_A;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_UP) != 0) {
        report->buttons |= SWITCH_X;
    }
    if ((controls.buttons & TAIKO_BUTTON_L1) != 0) {
        report->buttons |= SWITCH_L;
    }
    if ((controls.buttons & TAIKO_BUTTON_R1) != 0) {
        report->buttons |= SWITCH_R;
    }
    if ((controls.buttons & TAIKO_BUTTON_L2) != 0) {
        report->buttons |= SWITCH_ZL;
    }
    if ((controls.buttons & TAIKO_BUTTON_R2) != 0) {
        report->buttons |= SWITCH_ZR;
    }
    if ((controls.buttons & TAIKO_BUTTON_SELECT) != 0) {
        report->buttons |= SWITCH_MINUS;
    }
    if ((controls.buttons & TAIKO_BUTTON_START) != 0) {
        report->buttons |= SWITCH_PLUS;
    }
    if ((controls.buttons & TAIKO_BUTTON_HOME) != 0) {
        report->buttons |= SWITCH_HOME;
    }
}

void taiko_build_ps4_report(const taiko_input_snapshot_t *input,
                            uint8_t report_counter,
                            taiko_ps4_report_t *report) {
    const digital_controls_t controls = digital_controls(input);
    memset(report, 0, sizeof(*report));
    report->report_id = 0x01;
    report->left_x = 0x80;
    report->left_y = 0x80;
    report->right_x = 0x80;
    report->right_y = 0x80;
    report->buttons1 = console_hat(taiko_resolve_hat(controls.dpad));
    report->buttons3 = (uint8_t)((report_counter & 0x3fU) << 2);

    if ((controls.buttons & TAIKO_BUTTON_FACE_LEFT) != 0) {
        report->buttons1 |= PS4_SQUARE;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_DOWN) != 0) {
        report->buttons1 |= PS4_CROSS;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_RIGHT) != 0) {
        report->buttons1 |= PS4_CIRCLE;
    }
    if ((controls.buttons & TAIKO_BUTTON_FACE_UP) != 0) {
        report->buttons1 |= PS4_TRIANGLE;
    }
    if ((controls.buttons & TAIKO_BUTTON_L1) != 0) {
        report->buttons2 |= PS4_L1;
    }
    if ((controls.buttons & TAIKO_BUTTON_R1) != 0) {
        report->buttons2 |= PS4_R1;
    }
    if ((controls.buttons & TAIKO_BUTTON_L2) != 0) {
        report->buttons2 |= PS4_L2;
        report->left_trigger = UINT8_MAX;
    }
    if ((controls.buttons & TAIKO_BUTTON_R2) != 0) {
        report->buttons2 |= PS4_R2;
        report->right_trigger = UINT8_MAX;
    }
    if ((controls.buttons & TAIKO_BUTTON_SELECT) != 0) {
        report->buttons2 |= PS4_SHARE;
    }
    if ((controls.buttons & TAIKO_BUTTON_START) != 0) {
        report->buttons2 |= PS4_OPTIONS;
    }
    if ((controls.buttons & TAIKO_BUTTON_HOME) != 0) {
        report->buttons3 |= PS4_HOME;
    }
}

bool taiko_input_has_activity(const taiko_input_snapshot_t *input,
                              taiko_controller_mode_t mode) {
    if (input->dpad != 0 || input->buttons != 0) {
        return true;
    }
    if (mode == TAIKO_CONTROLLER_MODE_ARCADE) {
        return input->arcade_x != 0 || input->arcade_y != 0 ||
               input->arcade_rx != 0 || input->arcade_ry != 0;
    }
    return input->drum_buttons != 0;
}
