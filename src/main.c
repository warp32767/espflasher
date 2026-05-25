#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "pins.h"
#include "wifi.h"
#include "xbox.h"

static const char *TAG = "pfc";

void pfc_server_start(void);

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
