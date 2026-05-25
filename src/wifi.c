#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "wifi.h"

static const char *TAG = "pfc_wifi";

void wifi_init_softap(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	wifi_config_t wifi_config = {0};
	strncpy((char *)wifi_config.ap.ssid, PFC_AP_SSID, sizeof(wifi_config.ap.ssid));
	strncpy((char *)wifi_config.ap.password, PFC_AP_PASS, sizeof(wifi_config.ap.password));
	wifi_config.ap.ssid_len = strlen(PFC_AP_SSID);
	wifi_config.ap.channel = PFC_AP_CHANNEL;
	wifi_config.ap.max_connection = PFC_AP_MAX_CONN;
	wifi_config.ap.authmode = PFC_AP_AUTHMODE;

	if (strlen(PFC_AP_PASS) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

	ESP_LOGI(TAG, "softap ssid=%s channel=%d", PFC_AP_SSID, PFC_AP_CHANNEL);
}
