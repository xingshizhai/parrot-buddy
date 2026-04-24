#include <stdio.h>
#include "unity.h"
#include "agent_core.h"
#include "agent_events.h"

static int event_count = 0;

static void test_event_callback(const agent_event_t *evt) {
    event_count++;
    TEST_ASSERT_EQUAL(AGENT_EVT_AUDIO_DETECTED, evt->type);
}

TEST_CASE("Agent Core Event Queue", "[agent_core]") {
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_init());
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_register_callback(test_event_callback));
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_start());

    agent_event_t evt = {
        .type = AGENT_EVT_AUDIO_DETECTED,
        .timestamp_ms = 1000,
        .audio_detected = {
            .rms_level = -35.5f,
            .peak_frequency = 2500.0f
        }
    };

    event_count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_post_event(&evt));

    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, event_count);

    TEST_ASSERT_EQUAL(ESP_OK, agent_core_stop());
}
