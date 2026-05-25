#include "xbox.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pins.h"
#include "spiex.h"

static void sleep_ms(int ms)
{
	if (ms <= 0) {
		return;
	}
	vTaskDelay(pdMS_TO_TICKS(ms));
}

void xbox_init(void)
{
	spiex_init();

	gpio_config_t cfg = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = (1ULL << SMC_DBG_EN) | (1ULL << SMC_RST_XDK_N),
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE,
	};
	gpio_config(&cfg);

	gpio_set_level(SMC_DBG_EN, 0);
	gpio_set_level(SMC_RST_XDK_N, 1);
}

bool xbox_smc_stopped = false;

void xbox_start_smc(void)
{
	gpio_set_level(SMC_DBG_EN, 0);
	gpio_set_level(SMC_RST_XDK_N, 0);

	sleep_ms(50);

	gpio_set_level(SMC_RST_XDK_N, 1);

	xbox_smc_stopped = false;
}

void xbox_stop_smc(void)
{
	gpio_set_level(SMC_DBG_EN, 0);
	sleep_ms(50);

	gpio_set_level(SPI_SS_N, 0);
	gpio_set_level(SMC_RST_XDK_N, 0);
	sleep_ms(50);

	gpio_set_level(SMC_DBG_EN, 1);
	gpio_set_level(SMC_RST_XDK_N, 1);
	sleep_ms(50);

	gpio_set_level(SPI_SS_N, 1);
	sleep_ms(50);

	xbox_smc_stopped = true;
}

static uint32_t xbox_cached_flash_config;

uint32_t xbox_get_flash_config(void)
{
	if (!xbox_smc_stopped) {
		xbox_cached_flash_config = 0;
		return 0;
	}
	xbox_cached_flash_config = spiex_read_reg(0);
	if ((xbox_cached_flash_config & 0xF0000000) == 0xC0000000) {
		xbox_cached_flash_config = 0xC0462002;
	}
	return xbox_cached_flash_config;
}

static uint16_t xbox_nand_get_status(void)
{
	return spiex_read_reg(0x04);
}

static void xbox_nand_clear_status(void)
{
	spiex_write_reg(0x04, spiex_read_reg(0x04));
}

static int xbox_nand_wait_ready(uint16_t timeout)
{
	do {
		if (!(xbox_nand_get_status() & 0x01)) {
			return 0;
		}
	} while (timeout--);

	return 1;
}

int xbox_nand_read_block(uint32_t lba, uint8_t *buffer, uint8_t *spare)
{
	if (!xbox_smc_stopped || !xbox_cached_flash_config) {
		return 0;
	}

	xbox_nand_clear_status();

	spiex_write_reg(0x0C, lba << 9);
	spiex_write_reg(0x08, 0x03);

	if (xbox_nand_wait_ready(0x1000)) {
		return 0x8000 | xbox_nand_get_status();
	}

	spiex_write_reg(0x0C, 0);

	uint8_t *end = buffer + 0x200;
	while (buffer < end) {
		spiex_write_reg(0x08, 0x00);

		uint32_t v = spiex_read_reg(0x10);
		memcpy(buffer, &v, 4);
		buffer += 4;
	}

	end = spare + 0x10;
	while (spare < end) {
		spiex_write_reg(0x08, 0x00);

		uint32_t v = spiex_read_reg(0x10);
		memcpy(spare, &v, 4);
		spare += 4;
	}

	return 0;
}

int xbox_nand_erase_block(uint32_t lba)
{
	if (!xbox_smc_stopped || !xbox_cached_flash_config) {
		return 0;
	}

	xbox_nand_clear_status();

	spiex_write_reg(0x00, spiex_read_reg(0x00) | 0x08);

	spiex_write_reg(0x0C, lba << 9);

	spiex_write_reg(0x08, 0xAA);
	spiex_write_reg(0x08, 0x55);
	spiex_write_reg(0x08, 0x05);

	if (xbox_nand_wait_ready(0x1000)) {
		return 0x8000 | xbox_nand_get_status();
	}

	return 0;
}

int xbox_nand_write_block(uint32_t lba, uint8_t *buffer, uint8_t *spare)
{
	if (!xbox_smc_stopped || !xbox_cached_flash_config) {
		return 0;
	}

	int major = (xbox_cached_flash_config >> 17) & 3;
	int minor = (xbox_cached_flash_config >> 4) & 3;

	int blocksize = 0x4000;
	if (major >= 1) {
		if (minor == 2) {
			blocksize = 0x20000;
		} else if (minor == 3) {
			blocksize = 0x40000;
		}
	}

	int sectors_in_block = blocksize / 0x200;

	if (lba % sectors_in_block == 0) {
		int ret = xbox_nand_erase_block(lba);
		if (ret) {
			return ret;
		}
	}

	xbox_nand_clear_status();

	spiex_write_reg(0x0C, 0);

	uint8_t *end = buffer + 0x200;
	while (buffer < end) {
		uint32_t v;
		memcpy(&v, buffer, 4);
		spiex_write_reg(0x10, v);
		spiex_write_reg(0x08, 0x01);
		buffer += 4;
	}

	end = spare + 0x10;
	while (spare < end) {
		uint32_t v;
		memcpy(&v, spare, 4);
		spiex_write_reg(0x10, v);
		spiex_write_reg(0x08, 0x01);
		spare += 4;
	}

	if (xbox_nand_wait_ready(0x1000)) {
		return 0x8000 | xbox_nand_get_status();
	}

	spiex_write_reg(0x0C, lba << 9);

	if (xbox_nand_wait_ready(0x1000)) {
		return 0x8000 | xbox_nand_get_status();
	}

	spiex_write_reg(0x08, 0x55);
	spiex_write_reg(0x08, 0xAA);
	spiex_write_reg(0x08, 0x04);

	if (xbox_nand_wait_ready(0x1000)) {
		return 0x8000 | xbox_nand_get_status();
	}

	return 0;
}

#define SD_OK (0)
#define SD_ERR_TIMEOUT (-1)

static uint32_t xbox_emmc_get_ints(void)
{
	return spiex_read_reg(0x30);
}

static void xbox_emmc_clear_ints(uint32_t value)
{
	spiex_write_reg(0x30, value);
}

static void xbox_emmc_clear_all_ints(void)
{
	xbox_emmc_clear_ints(xbox_emmc_get_ints());
}

static int xbox_emmc_wait_ints(uint32_t value, int timeout_ms)
{
	int64_t start = esp_timer_get_time();
	while ((esp_timer_get_time() - start) < (int64_t)timeout_ms * 1000) {
		uint32_t ints = xbox_emmc_get_ints();
		if ((ints & value) == value) {
			return SD_OK;
		}
	}
	return SD_ERR_TIMEOUT;
}

static void xbox_emmc_execute(uint32_t reg_4, uint32_t reg_8, uint32_t reg_c)
{
	xbox_emmc_clear_all_ints();
	spiex_write_reg(0x04, reg_4);
	spiex_write_reg(0x08, reg_8);
	spiex_write_reg(0x0C, reg_c);
}

int xbox_emmc_init(void)
{
	spiex_write_reg(0x2C, spiex_read_reg(0x2C) | (1 << 24));
	int64_t start = esp_timer_get_time();
	while ((esp_timer_get_time() - start) < 5000LL * 1000) {
		if (spiex_read_reg(0x3C) & 0x1000000) {
			return SD_OK;
		}
	}
	return SD_ERR_TIMEOUT;
}

static int xbox_emmc_deselect_card(void)
{
	xbox_emmc_execute(0, 0, 0x7000000);
	return xbox_emmc_wait_ints(1, 100);
}

static int xbox_emmc_read_cid_csd(uint8_t *buf, int is_cid)
{
	xbox_emmc_execute(0, 0xffff0000, is_cid ? 0x9010000 : 0xA010000);
	int ret = xbox_emmc_wait_ints(1, 100);
	if (!ret) {
		for (int i = 0x10; i < 0x20; i += 4) {
			uint32_t data = spiex_read_reg(i);
			memcpy(buf, &data, 4);
			buf += 4;
		}
	}
	return ret;
}

int xbox_emmc_read_cid(uint8_t *cid)
{
	return xbox_emmc_read_cid_csd(cid, 1);
}

int xbox_emmc_read_csd(uint8_t *csd)
{
	return xbox_emmc_read_cid_csd(csd, 0);
}

static int xbox_emmc_select_card(void)
{
	xbox_emmc_execute(0, 0xffff0000, 0x71a0000);
	return xbox_emmc_wait_ints(1, 100);
}

static int xbox_emmc_set_blocklen(int blocklen)
{
	xbox_emmc_execute(0x200, blocklen, 0x101a0000);
	return xbox_emmc_wait_ints(1, 100);
}

static int xbox_emmc_read_block_ext_csd(uint8_t *buf, int block, int is_block)
{
	int ret = xbox_emmc_select_card();
	if (ret) {
		return ret;
	}
	if (is_block) {
		ret = xbox_emmc_set_blocklen(0x200);
		if (ret) {
			xbox_emmc_deselect_card();
			return ret;
		}
	}
	xbox_emmc_execute(0x10200, block << 9, is_block ? 0x113a0010 : 0x83A0010);
	ret = xbox_emmc_wait_ints(0x21, 1500);
	if (!ret) {
		for (int i = 0; i < 0x200; i += 4) {
			uint32_t data = spiex_read_reg(0x20);
			memcpy(buf + i, &data, 4);
		}
	}
	xbox_emmc_deselect_card();
	return ret;
}

int xbox_emmc_read_ext_csd(uint8_t *ext_csd)
{
	return xbox_emmc_read_block_ext_csd(ext_csd, 0, 0);
}

int xbox_emmc_read_block(int lba, uint8_t *buf)
{
	return xbox_emmc_read_block_ext_csd(buf, lba, 1);
}

int xbox_emmc_write_block(int lba, uint8_t *buf)
{
	int ret = xbox_emmc_select_card();
	if (ret) {
		return ret;
	}
	ret = xbox_emmc_set_blocklen(0x200);
	if (ret) {
		return ret;
	}
	xbox_emmc_execute(0x10200, lba << 9, 0x183a0000);
	ret = xbox_emmc_wait_ints(1, 100);
	if (!ret) {
		for (int i = 0; i < 0x200; i += 4) {
			uint32_t data;
			memcpy(&data, buf + i, 4);
			spiex_write_reg(0x20, data);
		}
		ret = xbox_emmc_wait_ints(0x12, 1500);
	}
	xbox_emmc_deselect_card();
	return ret;
}
