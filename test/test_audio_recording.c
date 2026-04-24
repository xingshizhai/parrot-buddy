#include <stdio.h>
#include "unity.h"
#include "audio_recording.h"

static int chunk_count = 0;
static bool recording_state = false;

static void test_chunk_callback(const int16_t *samples, size_t count, uint32_t timestamp_ms) {
    chunk_count++;
    TEST_ASSERT_TRUE(count > 0);
}

static void test_state_callback(bool is_recording, uint32_t duration_ms) {
    recording_state = is_recording;
}

TEST_CASE("Audio Recording Basic", "[audio_recording]") {
    recording_config_t config = {
        .sample_rate = 16000,
        .silence_timeout_ms = 5000,
        .min_duration_ms = 2000,
        .detection_threshold_db = -40.0f,
        .chunk_callback = test_chunk_callback,
        .state_callback = test_state_callback
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_recording_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_recording_start());

    TEST_ASSERT_FALSE(audio_recording_is_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_recording_stop());
}
