#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "pins.h"
#include "xbox.h"

#define PFC_AP_SSID "PicoFlasher"
#define PFC_AP_PASS "picoflasher"
#define PFC_AP_CHANNEL 6
#define PFC_AP_MAX_CONN 1
#define PFC_AP_IP "192.168.4.255"
#define PFC_AP_NETMASK "255.255.0.0"

static const char *TAG = "pfc";

void pfc_server_start(void);

static void wifi_init_softap(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *netif = esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));

	esp_netif_ip_info_t ip_info;
	memset(&ip_info, 0, sizeof(ip_info));
	ip4addr_aton(PFC_AP_IP, &ip_info.ip);
	ip4addr_aton(PFC_AP_IP, &ip_info.gw);
	ip4addr_aton(PFC_AP_NETMASK, &ip_info.netmask);
	ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
	ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));

	wifi_config_t wifi_config = {0};
	strncpy((char *)wifi_config.ap.ssid, PFC_AP_SSID, sizeof(wifi_config.ap.ssid));
	strncpy((char *)wifi_config.ap.password, PFC_AP_PASS, sizeof(wifi_config.ap.password));
	wifi_config.ap.ssid_len = strlen(PFC_AP_SSID);
	wifi_config.ap.channel = PFC_AP_CHANNEL;
	wifi_config.ap.max_connection = PFC_AP_MAX_CONN;
	wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

	if (strlen(PFC_AP_PASS) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "softap ssid=%s ip=%s", PFC_AP_SSID, PFC_AP_IP);
}

void app_main(void)
{
	gpio_config_t led_cfg = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = 1ULL << PFC_LED_PIN,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE,
	};
	gpio_config(&led_cfg);
	gpio_set_level(PFC_LED_PIN, 0);

	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	} else {
		ESP_ERROR_CHECK(ret);
	}

	wifi_init_softap();

	xbox_init();
	pfc_server_start();
}
