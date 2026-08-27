#include "taiko_hit_processor.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define QUIET_SAMPLE 194U
#define HIT_SAMPLE 1600U
#define TAIL_SAMPLE 420U
#define SCANS_PER_25_MS 261U

static bool push_samples(taiko_hit_processor_t *processor,
                         const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER],
                         taiko_hit_event_t *event) {
    return taiko_hit_processor_push(processor, samples, event);
}

static bool push_quiet(taiko_hit_processor_t *processor,
                       taiko_hit_event_t *event) {
    const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER] = {
        QUIET_SAMPLE, QUIET_SAMPLE, QUIET_SAMPLE, QUIET_SAMPLE,
    };
    return push_samples(processor, samples, event);
}

static bool push_zone(taiko_hit_processor_t *processor,
                      taiko_zone_t zone,
                      taiko_zone_t tail_zone,
                      taiko_hit_event_t *event) {
    uint16_t samples[TAIKO_CHANNELS_PER_PLAYER] = {
        QUIET_SAMPLE, QUIET_SAMPLE, QUIET_SAMPLE, QUIET_SAMPLE,
    };
    samples[zone] = HIT_SAMPLE;
    if (tail_zone != zone) {
        samples[tail_zone] = TAIL_SAMPLE;
    }
    return push_samples(processor, samples, event);
}

static void settle_baseline(taiko_hit_processor_t *processor) {
    for (unsigned scan = 0; scan < 32; ++scan) {
        taiko_hit_event_t event;
        assert(!push_quiet(processor, &event));
    }
}

static unsigned emit_zone(taiko_hit_processor_t *processor,
                          taiko_zone_t zone,
                          taiko_zone_t tail_zone) {
    for (unsigned scan = 0; scan < 32; ++scan) {
        taiko_hit_event_t event;
        if (push_zone(processor, zone, tail_zone, &event)) {
            assert(event.zone == zone);
            const taiko_hit_output_t output =
                taiko_hit_processor_get_output(processor);
            assert(output.active);
            assert(output.zone == zone);
            return scan;
        }
    }
    assert(!"hit was not emitted");
    return 0;
}

static void test_all_zone_pairs_at_25_ms(const taiko_hit_config_t *config) {
    for (taiko_zone_t first = TAIKO_ZONE_LEFT_DON;
         first <= TAIKO_ZONE_RIGHT_KA;
         first = (taiko_zone_t)(first + 1)) {
        for (taiko_zone_t second = TAIKO_ZONE_LEFT_DON;
             second <= TAIKO_ZONE_RIGHT_KA;
             second = (taiko_zone_t)(second + 1)) {
            taiko_hit_processor_t processor;
            taiko_hit_processor_init(&processor, config);
            settle_baseline(&processor);

            const unsigned first_event_scan =
                emit_zone(&processor, first, first);
            for (unsigned scan = first_event_scan + 1;
                 scan < SCANS_PER_25_MS;
                 ++scan) {
                taiko_hit_event_t event;
                assert(!push_quiet(&processor, &event));
            }
            assert(!taiko_hit_processor_get_output(&processor).active);

            const unsigned second_event_scan =
                emit_zone(&processor, second, second);
            assert(second_event_scan < 32);
        }
    }
}

static void test_other_zone_ignores_first_zone_tail(
    const taiko_hit_config_t *config) {
    taiko_hit_processor_t processor;
    taiko_hit_processor_init(&processor, config);
    settle_baseline(&processor);

    const unsigned first_event_scan = emit_zone(
        &processor, TAIKO_ZONE_RIGHT_DON, TAIKO_ZONE_RIGHT_DON);
    for (unsigned scan = first_event_scan + 1;
         scan < SCANS_PER_25_MS;
         ++scan) {
        const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER] = {
            QUIET_SAMPLE, QUIET_SAMPLE, TAIL_SAMPLE, QUIET_SAMPLE,
        };
        taiko_hit_event_t event;
        assert(!push_samples(&processor, samples, &event));
    }
    assert(!taiko_hit_processor_get_output(&processor).active);

    const unsigned second_event_scan = emit_zone(
        &processor, TAIKO_ZONE_LEFT_DON, TAIKO_ZONE_RIGHT_DON);
    assert(second_event_scan < 32);
}

static void test_tail_does_not_retrigger(const taiko_hit_config_t *config) {
    taiko_hit_processor_t processor;
    taiko_hit_processor_init(&processor, config);
    settle_baseline(&processor);
    (void)emit_zone(&processor, TAIKO_ZONE_LEFT_DON, TAIKO_ZONE_LEFT_DON);

    for (unsigned scan = 0; scan < 1000; ++scan) {
        const uint16_t samples[TAIKO_CHANNELS_PER_PLAYER] = {
            TAIL_SAMPLE, QUIET_SAMPLE, QUIET_SAMPLE, QUIET_SAMPLE,
        };
        taiko_hit_event_t event;
        assert(!push_samples(&processor, samples, &event));
    }
}

int main(void) {
    const taiko_hit_config_t standard = taiko_hit_default_config();
    const taiko_hit_config_t noisy = taiko_hit_long_tail_config();

    assert(standard.refractory_samples < SCANS_PER_25_MS);
    assert(noisy.refractory_samples < SCANS_PER_25_MS);
    test_all_zone_pairs_at_25_ms(&standard);
    test_all_zone_pairs_at_25_ms(&noisy);
    test_other_zone_ignores_first_zone_tail(&standard);
    test_other_zone_ignores_first_zone_tail(&noisy);
    test_tail_does_not_retrigger(&standard);
    test_tail_does_not_retrigger(&noisy);
    puts("taiko_hit_processor rapid-hit tests passed");
    return 0;
}
