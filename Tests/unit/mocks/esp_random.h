#ifndef MOCK_ESP_RANDOM_H
#define MOCK_ESP_RANDOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t esp_random(void);

#ifdef __cplusplus
}
#endif

#endif