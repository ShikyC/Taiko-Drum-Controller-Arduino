#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "taiko_reports.h"

static void test_hat_resolution(void) {
    static const struct {
        uint8_t dpad;
        taiko_hat_t hat;
    } cases[] = {
        {0, TAIKO_HAT_CENTERED},
        {TAIKO_DPAD_UP, TAIKO_HAT_UP},
        {TAIKO_DPAD_UP | TAIKO_DPAD_RIGHT, TAIKO_HAT_UP_RIGHT},
        {TAIKO_DPAD_RIGHT, TAIKO_HAT_RIGHT},
        {TAIKO_DPAD_DOWN | TAIKO_DPAD_RIGHT, TAIKO_HAT_DOWN_RIGHT},
        {TAIKO_DPAD_DOWN, TAIKO_HAT_DOWN},
        {TAIKO_DPAD_DOWN | TAIKO_DPAD_LEFT, TAIKO_HAT_DOWN_LEFT},
        {TAIKO_DPAD_LEFT, TAIKO_HAT_LEFT},
        {TAIKO_DPAD_UP | TAIKO_DPAD_LEFT, TAIKO_HAT_UP_LEFT},
        {TAIKO_DPAD_UP | TAIKO_DPAD_DOWN, TAIKO_HAT_CENTERED},
        {TAIKO_DPAD_LEFT | TAIKO_DPAD_RIGHT, TAIKO_HAT_CENTERED},
        {TAIKO_DPAD_UP | TAIKO_DPAD_DOWN | TAIKO_DPAD_RIGHT,
         TAIKO_HAT_RIGHT},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        assert(taiko_resolve_hat(cases[index].dpad) == cases[index].hat);
    }
}

static void test_arcade_report(void) {
    const taiko_input_snapshot_t input = {
        .dpad = TAIKO_DPAD_UP | TAIKO_DPAD_RIGHT,
        .buttons = TAIKO_BUTTON_FACE_UP | TAIKO_BUTTON_FACE_RIGHT |
                   TAIKO_BUTTON_FACE_DOWN | TAIKO_BUTTON_FACE_LEFT |
                   TAIKO_BUTTON_L1 | TAIKO_BUTTON_R1 | TAIKO_BUTTON_L2 |
                   TAIKO_BUTTON_R2 | TAIKO_BUTTON_SELECT |
                   TAIKO_BUTTON_START | TAIKO_BUTTON_HOME,
        // Drum buttons must not be folded into Arcade mode.
        .drum_buttons = TAIKO_DRUM_LEFT_DON | TAIKO_DRUM_RIGHT_DON |
                        TAIKO_DRUM_LEFT_KA | TAIKO_DRUM_RIGHT_KA,
        .arcade_x = 100,
        .arcade_y = -101,
        .arcade_rx = 102,
        .arcade_ry = -103,
    };
    taiko_arcade_report_t report;
    taiko_build_arcade_report(&input, &report);

    assert(report.x == 100);
    assert(report.y == -101);
    assert(report.z == 0);
    assert(report.rz == 0);
    assert(report.rx == 102);
    assert(report.ry == -103);
    assert(report.hat == TAIKO_HAT_UP_RIGHT);
    assert(report.buttons == 0x1fdbU);
}

static void test_physical_controls(void) {
    const taiko_input_snapshot_t input = {
        .dpad = TAIKO_DPAD_UP | TAIKO_DPAD_RIGHT,
        .buttons = TAIKO_BUTTON_FACE_UP | TAIKO_BUTTON_FACE_RIGHT |
                   TAIKO_BUTTON_FACE_DOWN | TAIKO_BUTTON_FACE_LEFT |
                   TAIKO_BUTTON_L1 | TAIKO_BUTTON_R1 | TAIKO_BUTTON_L2 |
                   TAIKO_BUTTON_R2 | TAIKO_BUTTON_SELECT |
                   TAIKO_BUTTON_START | TAIKO_BUTTON_HOME,
    };

    taiko_xinput_report_t xinput;
    taiko_build_xinput_report(&input, &xinput);
    assert(xinput.report_id == 0);
    assert(xinput.report_size == sizeof(xinput));
    assert(xinput.buttons1 == 0x39U);
    assert(xinput.buttons2 == 0xf7U);
    assert(xinput.left_trigger == UINT8_MAX);
    assert(xinput.right_trigger == UINT8_MAX);
    assert(xinput.left_x == 0 && xinput.left_y == 0);
    assert(xinput.right_x == 0 && xinput.right_y == 0);

    taiko_switch_report_t switch_report;
    taiko_build_switch_report(&input, &switch_report);
    assert(switch_report.buttons == 0x13ffU);
    assert(switch_report.hat == 1U);
    assert(switch_report.left_x == 0x80 && switch_report.left_y == 0x80);
    assert(switch_report.right_x == 0x80 && switch_report.right_y == 0x80);

    taiko_ps4_report_t ps4;
    taiko_build_ps4_report(&input, 0x2a, &ps4);
    assert(ps4.report_id == 0x01);
    assert(ps4.left_x == 0x80 && ps4.left_y == 0x80);
    assert(ps4.right_x == 0x80 && ps4.right_y == 0x80);
    assert(ps4.buttons1 == 0xf1U);
    assert(ps4.buttons2 == 0x3fU);
    assert(ps4.buttons3 == 0xa9U);
    assert(ps4.left_trigger == UINT8_MAX);
    assert(ps4.right_trigger == UINT8_MAX);
}

static void test_individual_button_names(void) {
    static const struct {
        uint16_t input_button;
        uint8_t xinput_buttons1;
        uint8_t xinput_buttons2;
        uint8_t xinput_left_trigger;
        uint8_t xinput_right_trigger;
        uint16_t switch_buttons;
        uint8_t ps4_buttons1;
        uint8_t ps4_buttons2;
        uint8_t ps4_buttons3;
        uint8_t ps4_left_trigger;
        uint8_t ps4_right_trigger;
    } cases[] = {
        {TAIKO_BUTTON_FACE_UP, 0, 1U << 7, 0, 0, 1U << 3, 0x88, 0, 0, 0,
         0},
        {TAIKO_BUTTON_FACE_RIGHT, 0, 1U << 5, 0, 0, 1U << 2, 0x48, 0, 0,
         0, 0},
        {TAIKO_BUTTON_FACE_DOWN, 0, 1U << 4, 0, 0, 1U << 1, 0x28, 0, 0, 0,
         0},
        {TAIKO_BUTTON_FACE_LEFT, 0, 1U << 6, 0, 0, 1U << 0, 0x18, 0, 0, 0,
         0},
        {TAIKO_BUTTON_L1, 0, 1U << 0, 0, 0, 1U << 4, 8, 1U << 0, 0, 0,
         0},
        {TAIKO_BUTTON_R1, 0, 1U << 1, 0, 0, 1U << 5, 8, 1U << 1, 0, 0,
         0},
        {TAIKO_BUTTON_L2, 0, 0, UINT8_MAX, 0, 1U << 6, 8, 1U << 2, 0,
         UINT8_MAX, 0},
        {TAIKO_BUTTON_R2, 0, 0, 0, UINT8_MAX, 1U << 7, 8, 1U << 3, 0, 0,
         UINT8_MAX},
        {TAIKO_BUTTON_SELECT, 1U << 5, 0, 0, 0, 1U << 8, 8, 1U << 4, 0,
         0, 0},
        {TAIKO_BUTTON_START, 1U << 4, 0, 0, 0, 1U << 9, 8, 1U << 5, 0, 0,
         0},
        {TAIKO_BUTTON_HOME, 0, 1U << 2, 0, 0, 1U << 12, 8, 0, 1U << 0, 0,
         0},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const taiko_input_snapshot_t input = {
            .buttons = cases[index].input_button,
        };

        taiko_xinput_report_t xinput;
        taiko_build_xinput_report(&input, &xinput);
        assert(xinput.buttons1 == cases[index].xinput_buttons1);
        assert(xinput.buttons2 == cases[index].xinput_buttons2);
        assert(xinput.left_trigger == cases[index].xinput_left_trigger);
        assert(xinput.right_trigger == cases[index].xinput_right_trigger);

        taiko_switch_report_t switch_report;
        taiko_build_switch_report(&input, &switch_report);
        assert(switch_report.buttons == cases[index].switch_buttons);
        assert(switch_report.hat == 8);

        taiko_ps4_report_t ps4;
        taiko_build_ps4_report(&input, 0, &ps4);
        assert(ps4.buttons1 == cases[index].ps4_buttons1);
        assert(ps4.buttons2 == cases[index].ps4_buttons2);
        assert(ps4.buttons3 == cases[index].ps4_buttons3);
        assert(ps4.left_trigger == cases[index].ps4_left_trigger);
        assert(ps4.right_trigger == cases[index].ps4_right_trigger);
    }
}

static void test_requested_drum_mapping(void) {
    static const struct {
        uint8_t drum;
        uint8_t xinput_buttons1;
        uint8_t xinput_buttons2;
        uint16_t switch_buttons;
        uint8_t switch_hat;
        uint8_t ps4_buttons1;
        uint8_t ps4_buttons2;
    } cases[] = {
        {TAIKO_DRUM_LEFT_DON, 1U << 1, 0, 0, 4, 4, 0},
        {TAIKO_DRUM_RIGHT_DON, 0, 1U << 4, 1U << 1, 8, 0x28, 0},
        {TAIKO_DRUM_LEFT_KA, 0, 1U << 0, 1U << 4, 8, 8, 1U << 0},
        {TAIKO_DRUM_RIGHT_KA, 0, 1U << 1, 1U << 5, 8, 8, 1U << 1},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const taiko_input_snapshot_t input = {
            .drum_buttons = cases[index].drum,
        };

        taiko_xinput_report_t xinput;
        taiko_build_xinput_report(&input, &xinput);
        assert(xinput.buttons1 == cases[index].xinput_buttons1);
        assert(xinput.buttons2 == cases[index].xinput_buttons2);

        taiko_switch_report_t switch_report;
        taiko_build_switch_report(&input, &switch_report);
        assert(switch_report.buttons == cases[index].switch_buttons);
        assert(switch_report.hat == cases[index].switch_hat);

        taiko_ps4_report_t ps4;
        taiko_build_ps4_report(&input, 0, &ps4);
        assert(ps4.buttons1 == cases[index].ps4_buttons1);
        assert(ps4.buttons2 == cases[index].ps4_buttons2);
    }
}

static void test_activity_detection(void) {
    taiko_input_snapshot_t input = {0};
    assert(!taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_ARCADE));
    assert(!taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_PC));

    input.arcade_rx = -1;
    assert(taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_ARCADE));
    assert(!taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_PC));

    input.arcade_rx = 0;
    input.drum_buttons = TAIKO_DRUM_RIGHT_KA;
    assert(!taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_ARCADE));
    assert(taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_PC));
    assert(taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_SWITCH));
    assert(taiko_input_has_activity(&input, TAIKO_CONTROLLER_MODE_PS4));
}

int main(void) {
    test_hat_resolution();
    test_arcade_report();
    test_physical_controls();
    test_individual_button_names();
    test_requested_drum_mapping();
    test_activity_detection();
    puts("taiko report tests passed");
    return 0;
}
