#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define PARROT_MAGIC 0x50415252
#define PARROT_VERSION 0x01

#define PARROT_TYPE_AUDIO 0x01
#define PARROT_TYPE_CONTROL 0x02

#define PARROT_HEADER_SIZE 16

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t reserved;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t length;
    uint16_t checksum;
} parrot_header_t;

esp_err_t parrot_protocol_encode_audio(const int16_t *pcm_samples, size_t num_samples,
                                       uint32_t timestamp_ms, uint32_t sequence,
                                       uint8_t *output, size_t output_max_len, size_t *output_len);

esp_err_t parrot_protocol_decode_header(const uint8_t *data, size_t len, parrot_header_t *header);

esp_err_t parrot_protocol_decode_audio(const uint8_t *data, size_t len,
                                       int16_t *pcm_out, size_t pcm_max, size_t *pcm_written);

uint16_t parrot_protocol_crc16(const uint8_t *data, size_t len);
