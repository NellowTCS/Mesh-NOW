cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_now)

target_sources(app PRIVATE src/main.c)

# Enable ESP-IDF components if building for ESP32
if(CONFIG_SOC_SERIES_ESP32 OR CONFIG_SOC_SERIES_ESP32S2 OR CONFIG_SOC_SERIES_ESP32S3 OR CONFIG_SOC_SERIES_ESP32C3 OR CONFIG_SOC_SERIES_ESP32C6)
  # ESP-NOW is part of ESP-IDF and will be available through Zephyr's ESP32 HAL
  zephyr_include_directories(${ZEPHYR_BASE}/../modules/hal/espressif/components/esp_wifi/include)
  zephyr_include_directories(${ZEPHYR_BASE}/../modules/hal/espressif/components/esp_netif/include)
  zephyr_include_directories(${ZEPHYR_BASE}/../modules/hal/espressif/components/nvs_flash/include)
endif()