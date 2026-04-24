#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "transport.h"

#define TAG "TRANSPORT"

#define RECONNECT_DELAY_MS      3000
#define SOCKET_TIMEOUT_MS        5000
#define SHAKEHAND_TIMEOUT_MS     5000
#define MAX_FRAME_SIZE           4096

static const char *WS_MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static transport_config_t s_config = {0};
static transport_state_t s_state = TRANSPORT_STATE_DISCONNECTED;
static TaskHandle_t s_tx_task = NULL;
static TaskHandle_t s_rx_task = NULL;
static TaskHandle_t s_connect_task = NULL;
static QueueHandle_t s_audio_queue = NULL;
static bool s_running = false;
static int s_socket = -1;

static uint8_t s_rx_buf[MAX_FRAME_SIZE];

typedef struct {
    uint8_t *data;
    size_t len;
    uint32_t timestamp_ms;
} audio_buffer_t;

static uint8_t s_base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, size_t in_len, char *out) {
    size_t i, j;
    for (i = 0, j = 0; i < in_len; i += 3) {
        int a = in[i];
        int b = (i + 1 < in_len) ? in[i + 1] : 0;
        int c = (i + 2 < in_len) ? in[i + 2] : 0;
        out[j++] = s_base64_table[(a >> 2) & 0x3F];
        out[j++] = s_base64_table[((a << 4) | (b >> 4)) & 0x3F];
        out[j++] = (i + 1 < in_len) ? s_base64_table[((b << 2) | (c >> 6)) & 0x3F] : '=';
        out[j++] = (i + 2 < in_len) ? s_base64_table[c & 0x3F] : '=';
    }
    out[j] = '\0';
}

static void sha1_hash(const char *input, size_t len, uint8_t *output) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    uint32_t w[80];
    uint8_t msg[64];
    size_t msg_len = ((len + 8) / 64 + 1) * 64;
    memset(msg, 0, msg_len);
    memcpy(msg, input, len);
    msg[len] = 0x80;
    uint32_t bits = len * 8;
    memset(msg + msg_len - 4, 0, 4);
    msg[msg_len - 4] = (bits >> 24) & 0xFF;
    msg[msg_len - 3] = (bits >> 16) & 0xFF;
    msg[msg_len - 2] = (bits >> 8) & 0xFF;
    msg[msg_len - 1] = bits & 0xFF;
    for (size_t i = 0; i < msg_len; i += 64) {
        memset(w, 0, sizeof(w));
        for (size_t t = 0; t < 16; t++) {
            w[t] = (msg[i + t * 4] << 24) | (msg[i + t * 4 + 1] << 16) | (msg[i + t * 4 + 2] << 8) | msg[i + t * 4 + 3];
        }
        for (size_t t = 16; t < 80; t++) {
            w[t] = ((w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16]) << 1) | ((w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16]) >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (size_t t = 0; t < 80; t++) {
            uint32_t f, k;
            if (t < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (t < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[t];
            e = d; d = c; c = ((b << 30) | (b >> 2)); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    output[0] = (h0 >> 24) & 0xFF; output[1] = (h0 >> 16) & 0xFF; output[2] = (h0 >> 8) & 0xFF; output[3] = h0 & 0xFF;
    output[4] = (h1 >> 24) & 0xFF; output[5] = (h1 >> 16) & 0xFF; output[6] = (h1 >> 8) & 0xFF; output[7] = h1 & 0xFF;
    output[8] = (h2 >> 24) & 0xFF; output[9] = (h2 >> 16) & 0xFF; output[10] = (h2 >> 8) & 0xFF; output[11] = h2 & 0xFF;
    output[12] = (h3 >> 24) & 0xFF; output[13] = (h3 >> 16) & 0xFF; output[14] = (h3 >> 8) & 0xFF; output[15] = h3 & 0xFF;
    output[16] = (h4 >> 24) & 0xFF; output[17] = (h4 >> 16) & 0xFF; output[18] = (h4 >> 8) & 0xFF; output[19] = h4 & 0xFF;
}

static int tcp_connect(const char *host, uint16_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he) {
            ESP_LOGE(TAG, "DNS lookup failed for %s", host);
            return -1;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return -1;
    }
    struct timeval tv;
    tv.tv_sec = SOCKET_TIMEOUT_MS / 1000;
    tv.tv_usec = (SOCKET_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "connect() to %s:%d failed", host, port);
        close(sock);
        return -1;
    }
    ESP_LOGI(TAG, "TCP connected to %s:%d", host, port);
    return sock;
}

static bool ws_handshake(int sock, const char *host, uint16_t port, const char *path) {
    uint8_t key_bytes[16];
    for (int i = 0; i < 16; i++) {
        key_bytes[i] = esp_random() & 0xFF;
    }
    char key_b64[25];
    base64_encode(key_bytes, 16, key_b64);
    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, port, key_b64);
    if (send(sock, req, req_len, 0) != req_len) {
        ESP_LOGE(TAG, "Failed to send handshake request");
        return false;
    }
    char resp[512];
    int resp_len = recv(sock, resp, sizeof(resp) - 1, 0);
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "Handshake recv failed");
        return false;
    }
    resp[resp_len] = '\0';
    if (strstr(resp, "101 Switching Protocols") == NULL) {
        ESP_LOGE(TAG, "Handshake failed - no 101 response");
        ESP_LOGD(TAG, "Response: %s", resp);
        return false;
    }
    char *accept = strstr(resp, "Sec-WebSocket-Accept: ");
    if (!accept) {
        ESP_LOGE(TAG, "No Sec-WebSocket-Accept in response");
        return false;
    }
    accept += 21;
    char accept_key[32];
    for (int i = 0; i < 32 && accept[i] != '\r' && accept[i] != '\n'; i++) {
        accept_key[i] = accept[i];
    }
    accept_key[29] = '\0';
    char expected_b64[32];
    char combined[64];
    snprintf(combined, sizeof(combined), "%s%s", key_b64, WS_MAGIC_STRING);
    uint8_t sha[20];
    sha1_hash(combined, strlen(combined), sha);
    base64_encode(sha, 20, expected_b64);
    if (strncmp(accept_key, expected_b64, 29) != 0) {
        ESP_LOGW(TAG, "Accept key mismatch (this is informational)");
    }
    ESP_LOGI(TAG, "WebSocket handshake complete");
    return true;
}

static int ws_send_frame(int sock, const uint8_t *data, size_t len, int opcode) {
    if (len > 125) {
        ESP_LOGE(TAG, "Payload too large for single frame: %zu", len);
        return -1;
    }
    uint8_t mask_bit = 1;
    uint8_t header[6];
    header[0] = 0x80 | opcode;
    header[1] = (mask_bit << 7) | (uint8_t)len;
    uint8_t mask_key[4];
    mask_key[0] = esp_random() & 0xFF;
    mask_key[1] = esp_random() & 0xFF;
    mask_key[2] = esp_random() & 0xFF;
    mask_key[3] = esp_random() & 0xFF;
    header[2] = mask_key[0];
    header[3] = mask_key[1];
    header[4] = mask_key[2];
    header[5] = mask_key[3];
    uint8_t frame[6 + 125];
    memcpy(frame, header, 6);
    uint8_t masked[125];
    for (size_t i = 0; i < len; i++) {
        masked[i] = data[i] ^ mask_key[i % 4];
    }
    memcpy(frame + 6, masked, len);
    int sent = send(sock, frame, 6 + len, 0);
    if (sent != (int)(6 + len)) {
        ESP_LOGE(TAG, "Failed to send full frame");
        return -1;
    }
    return 0;
}

static int ws_recv_frame(int sock, uint8_t *out_data, size_t max_len, size_t *out_len) {
    uint8_t header[2];
    int ret = recv(sock, header, 2, 0);
    if (ret != 2) {
        return -1;
    }
    int fin = (header[0] & 0x80) != 0;
    int opcode = header[0] & 0x0F;
    int masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    if (payload_len == 126) {
        uint8_t ext[2];
        if (recv(sock, ext, 2, 0) != 2) return -1;
        payload_len = (ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (recv(sock, ext, 8, 0) != 8) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }
    uint8_t mask_key[4] = {0};
    if (masked) {
        if (recv(sock, mask_key, 4, 0) != 4) return -1;
    }
    if (payload_len > max_len) {
        ESP_LOGE(TAG, "Frame payload %llu > buffer %zu", payload_len, max_len);
        return -1;
    }
    size_t received = 0;
    while (received < payload_len) {
        ret = recv(sock, out_data + received, payload_len - received, 0);
        if (ret <= 0) return -1;
        received += ret;
    }
    if (masked) {
        for (size_t i = 0; i < payload_len; i++) {
            out_data[i] ^= mask_key[i % 4];
        }
    }
    *out_len = payload_len;
    if (opcode == 0x08) {
        return -1;
    }
    return opcode;
}

static void rx_task_func(void *arg) {
    while (s_running && s_socket >= 0) {
        size_t frame_len = 0;
        int opcode = ws_recv_frame(s_socket, s_rx_buf, MAX_FRAME_SIZE, &frame_len);
        if (opcode < 0) {
            ESP_LOGW(TAG, "Connection lost (rx)");
            break;
        }
        if (opcode == 0x01 || opcode == 0x02) {
            if (frame_len > 0 && s_config.data_callback) {
                s_config.data_callback(s_rx_buf, frame_len, s_config.ctx);
            }
        }
    }
    if (s_running) {
        s_state = TRANSPORT_STATE_DISCONNECTED;
        if (s_config.state_callback) {
            s_config.state_callback(s_state, s_config.ctx);
        }
    }
    vTaskDelete(NULL);
}

static void tx_task_func(void *arg) {
    audio_buffer_t buffer;
    while (s_running) {
        if (xQueueReceive(s_audio_queue, &buffer, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_state == TRANSPORT_STATE_CONNECTED && s_socket >= 0) {
                if (ws_send_frame(s_socket, buffer.data, buffer.len, 0x02) < 0) {
                    ESP_LOGW(TAG, "Failed to send audio frame");
                }
            }
        }
    }
    vTaskDelete(NULL);
}

static void connect_task_func(void *arg) {
    ESP_LOGI(TAG, "Connect task started");
    while (s_running) {
        s_state = TRANSPORT_STATE_CONNECTING;
        if (s_config.state_callback) {
            s_config.state_callback(s_state, s_config.ctx);
        }
        ESP_LOGI(TAG, "Connecting to WebSocket server %s:%d...", 
                 s_config.server_host ? s_config.server_host : "unknown",
                 s_config.server_port);
        s_socket = tcp_connect(s_config.server_host, s_config.server_port);
        if (s_socket < 0) {
            ESP_LOGW(TAG, "TCP connect failed, retrying in %d ms", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }
        if (!ws_handshake(s_socket, s_config.server_host, s_config.server_port,
                          s_config.ws_path ? s_config.ws_path : "/")) {
            close(s_socket);
            s_socket = -1;
            ESP_LOGW(TAG, "WS handshake failed, retrying in %d ms", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            continue;
        }
        s_state = TRANSPORT_STATE_CONNECTED;
        if (s_config.state_callback) {
            s_config.state_callback(s_state, s_config.ctx);
        }
        xTaskCreate(rx_task_func, "transport_rx", 4096, NULL, 5, &s_rx_task);
        while (s_running && s_socket >= 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (s_rx_task) {
            vTaskDelete(s_rx_task);
            s_rx_task = NULL;
        }
        if (s_socket >= 0) {
            close(s_socket);
            s_socket = -1;
        }
        if (s_running) {
            s_state = TRANSPORT_STATE_DISCONNECTED;
            if (s_config.state_callback) {
                s_config.state_callback(s_state, s_config.ctx);
            }
            ESP_LOGI(TAG, "Disconnected, reconnecting in %d ms", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        }
    }
    vTaskDelete(NULL);
}

esp_err_t transport_init(const transport_config_t *config) {
    if (!config || !config->server_host || !config->server_port) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    s_audio_queue = xQueueCreate(16, sizeof(audio_buffer_t));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        return ESP_FAIL;
    }
    s_state = TRANSPORT_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "Transport initialized for %s:%d", config->server_host, config->server_port);
    return ESP_OK;
}

esp_err_t transport_start(void) {
    if (s_running) {
        return ESP_OK;
    }
    s_running = true;
    xTaskCreate(tx_task_func, "transport_tx", 4096, NULL, 5, &s_tx_task);
    xTaskCreate(connect_task_func, "transport_conn", 8192, NULL, 4, &s_connect_task);
    ESP_LOGI(TAG, "Transport started");
    return ESP_OK;
}

esp_err_t transport_stop(void) {
    if (!s_running) {
        return ESP_OK;
    }
    s_running = false;
    if (s_socket >= 0) {
        shutdown(s_socket, SHUT_RDWR);
        close(s_socket);
        s_socket = -1;
    }
    if (s_tx_task) {
        vTaskDelete(s_tx_task);
        s_tx_task = NULL;
    }
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }
    if (s_connect_task) {
        vTaskDelete(s_connect_task);
        s_connect_task = NULL;
    }
    if (s_audio_queue) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }
    s_state = TRANSPORT_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "Transport stopped");
    return ESP_OK;
}

esp_err_t transport_send_audio(const uint8_t *data, size_t len, uint32_t timestamp_ms) {
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_running || !s_audio_queue) {
        return ESP_FAIL;
    }
    audio_buffer_t buffer = {
        .data = (uint8_t *)data,
        .len = len,
        .timestamp_ms = timestamp_ms
    };
    if (xQueueSend(s_audio_queue, &buffer, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Audio queue full, dropping frame");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t transport_send_control(const char *json_msg) {
    if (!json_msg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state != TRANSPORT_STATE_CONNECTED || s_socket < 0) {
        return ESP_FAIL;
    }
    if (ws_send_frame(s_socket, (const uint8_t *)json_msg, strlen(json_msg), 0x01) < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

transport_state_t transport_get_state(void) {
    return s_state;
}

bool transport_is_connected(void) {
    return s_state == TRANSPORT_STATE_CONNECTED;
}
