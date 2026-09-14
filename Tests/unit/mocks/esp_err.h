#ifndef MOCK_ESP_ERR_H
#define MOCK_ESP_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;

#define ESP_OK                 0
#define ESP_FAIL               -1
#define ESP_ERR_NO_MEM         0x101
#define ESP_ERR_INVALID_ARG    0x102
#define ESP_ERR_INVALID_SIZE   0x103
#define ESP_ERR_NOT_FOUND      0x105
#define ESP_ERR_ESPNOW_EXIST   0x3f0
#define ESP_ERR_ESPNOW_NOT_FOUND 0x3f1

const char *esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}
#endif

#endif