#pragma once

#include "esp_wifi_types_generic.h"

#define PFC_AP_SSID "PicoFlasher"
#define PFC_AP_PASS "picoflasher"
#define PFC_AP_CHANNEL 6
#define PFC_AP_MAX_CONN 1
#define PFC_AP_AUTHMODE WIFI_AUTH_WPA_WPA2_PSK

void wifi_init_softap(void);
