#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "esp_log.h"

#include "pfc_proto.h"
#include "xbox.h"

#define GET_VERSION 0x00
#define GET_FLASH_CONFIG 0x01
#define READ_FLASH 0x02
#define WRITE_FLASH 0x03
#define READ_FLASH_STREAM 0x04
#define ERASE_FLASH 0x05
#define WRITE_FLASH_MULTI 0x06

#define SET_SMC_WORKAROUND 0x20
#define STOP_SMC 0x21
#define START_SMC 0x22

#define EMMC_DETECT 0x50
#define EMMC_INIT 0x51
#define EMMC_GET_CID 0x52
#define EMMC_GET_CSD 0x53
#define EMMC_GET_EXT_CSD 0x54
#define EMMC_READ 0x55
#define EMMC_READ_STREAM 0x56
#define EMMC_WRITE 0x57
#define EMMC_WRITE_MULTI 0x58

#define PFC_TCP_PORT 3232
#define PFC_MAX_PAYLOAD 8192
#define PFC_NAND_BLOCK_BYTES 0x210
#define PFC_EMMC_BLOCK_BYTES 0x200

static const char *TAG = "pfc";

static int recv_all(int sock, void *buf, size_t len)
{
	uint8_t *p = (uint8_t *)buf;
	size_t left = len;
	while (left) {
		int r = recv(sock, p, left, 0);
		if (r == 0) {
			return -1;
		}
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		p += (size_t)r;
		left -= (size_t)r;
	}
	return 0;
}

static int send_all(int sock, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t left = len;
	while (left) {
		int r = send(sock, p, left, 0);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		p += (size_t)r;
		left -= (size_t)r;
	}
	return 0;
}

static int pfc_send_frame(int sock, uint16_t type, const void *payload, uint32_t payload_len)
{
	pfc_hdr_t hdr = {
		.magic = PFC_MAGIC,
		.version = PFC_VERSION,
		.type = type,
		.length = payload_len,
	};

	if (send_all(sock, &hdr, sizeof(hdr))) {
		return -1;
	}
	if (payload_len && send_all(sock, payload, payload_len)) {
		return -1;
	}
	return 0;
}

static int pfc_send_u32(int sock, uint32_t v)
{
	return pfc_send_frame(sock, PFC_MSG_RESPONSE, &v, sizeof(v));
}

static int pfc_send_bytes(int sock, const void *data, uint32_t data_len)
{
	return pfc_send_frame(sock, PFC_MSG_RESPONSE, data, data_len);
}

static int pfc_send_u32_with_data(int sock, uint32_t v, const void *data, uint32_t data_len)
{
	pfc_hdr_t hdr = {
		.magic = PFC_MAGIC,
		.version = PFC_VERSION,
		.type = PFC_MSG_RESPONSE,
		.length = 4 + data_len,
	};

	if (send_all(sock, &hdr, sizeof(hdr))) {
		return -1;
	}
	if (send_all(sock, &v, 4)) {
		return -1;
	}
	if (data_len && send_all(sock, data, data_len)) {
		return -1;
	}
	return 0;
}

static bool enable_smc_workaround = true;

static void maybe_stop_smc(void)
{
	if (enable_smc_workaround && !xbox_smc_stopped) {
		xbox_stop_smc();
	}
}

static int handle_cmd(int sock, const uint8_t *payload, uint32_t payload_len)
{
	if (payload_len < sizeof(pfc_cmd_t)) {
		return -1;
	}

	pfc_cmd_t cmd;
	memcpy(&cmd, payload, sizeof(cmd));

	const uint8_t *extra = payload + sizeof(cmd);
	uint32_t extra_len = payload_len - sizeof(cmd);

	if (cmd.cmd == GET_VERSION) {
		uint32_t ver = 4;
		return pfc_send_u32(sock, ver);
	}

	if (cmd.cmd == SET_SMC_WORKAROUND) {
		enable_smc_workaround = cmd.lba & 1;
		return pfc_send_u32(sock, 0);
	}

	if (cmd.cmd == STOP_SMC) {
		xbox_stop_smc();
		return pfc_send_u32(sock, 0);
	}

	if (cmd.cmd == START_SMC) {
		xbox_start_smc();
		return pfc_send_u32(sock, 0);
	}

	if (cmd.cmd == GET_FLASH_CONFIG) {
		maybe_stop_smc();
		uint32_t fc = xbox_get_flash_config();
		return pfc_send_u32(sock, fc);
	}

	if (cmd.cmd == READ_FLASH) {
		maybe_stop_smc();
		uint8_t buffer[PFC_NAND_BLOCK_BYTES];
		uint32_t ret = xbox_nand_read_block(cmd.lba, buffer, &buffer[0x200]);
		if (ret == 0) {
			return pfc_send_u32_with_data(sock, ret, buffer, sizeof(buffer));
		}
		return pfc_send_u32(sock, ret);
	}

	if (cmd.cmd == WRITE_FLASH) {
		maybe_stop_smc();
		if (extra_len != PFC_NAND_BLOCK_BYTES) {
			return pfc_send_u32(sock, 0xFFFFFFFFu);
		}
		uint32_t ret = xbox_nand_write_block(cmd.lba, (uint8_t *)extra, (uint8_t *)(extra + 0x200));
		return pfc_send_u32(sock, ret);
	}

	if (cmd.cmd == WRITE_FLASH_MULTI) {
		maybe_stop_smc();
		if (extra_len < 2) {
			return pfc_send_u32(sock, 0xFFFFFFFFu);
		}
		uint16_t count;
		memcpy(&count, extra, sizeof(count));
		const uint8_t *data = extra + 2;
		uint32_t data_len = extra_len - 2;
		if (count == 0) {
			return pfc_send_u32(sock, 0);
		}
		if (data_len != (uint32_t)count * PFC_NAND_BLOCK_BYTES) {
			return pfc_send_u32(sock, 0xFFFFFFFFu);
		}
		for (uint16_t i = 0; i < count; i++) {
			const uint8_t *blk = data + (uint32_t)i * PFC_NAND_BLOCK_BYTES;
			uint32_t ret = xbox_nand_write_block(cmd.lba + i, (uint8_t *)blk, (uint8_t *)(blk + 0x200));
			if (ret != 0) {
				uint32_t resp[2] = {ret, i};
				return pfc_send_bytes(sock, resp, sizeof(resp));
			}
		}
		{
			uint32_t resp[2] = {0, count};
			return pfc_send_bytes(sock, resp, sizeof(resp));
		}
	}

	if (cmd.cmd == ERASE_FLASH) {
		maybe_stop_smc();
		uint32_t ret = xbox_nand_erase_block(cmd.lba);
		return pfc_send_u32(sock, ret);
	}

	if (cmd.cmd == READ_FLASH_STREAM) {
		maybe_stop_smc();
		uint32_t end = cmd.lba;
		for (uint32_t i = 0; i < end; i++) {
			uint8_t buffer[PFC_NAND_BLOCK_BYTES];
			uint32_t ret = xbox_nand_read_block(i, buffer, &buffer[0x200]);
			if (ret == 0) {
				if (pfc_send_u32_with_data(sock, ret, buffer, sizeof(buffer))) {
					return -1;
				}
			} else {
				return pfc_send_u32(sock, ret);
			}
		}
		return 0;
	}

	if (cmd.cmd == EMMC_DETECT) {
		maybe_stop_smc();
		uint32_t fc = xbox_get_flash_config();
		uint8_t emmc_detect_result = (fc & 0xF0000000) == 0xC0000000;
		return pfc_send_bytes(sock, &emmc_detect_result, sizeof(emmc_detect_result));
	}

	if (cmd.cmd == EMMC_INIT) {
		maybe_stop_smc();
		uint32_t ret = (uint32_t)xbox_emmc_init();
		return pfc_send_u32(sock, ret);
	}

	if (cmd.cmd == EMMC_GET_CID) {
		maybe_stop_smc();
		uint8_t cid_raw[16] = {0};
		xbox_emmc_read_cid(cid_raw);
		return pfc_send_frame(sock, PFC_MSG_RESPONSE, cid_raw, sizeof(cid_raw));
	}

	if (cmd.cmd == EMMC_GET_CSD) {
		maybe_stop_smc();
		uint8_t csd_raw[16] = {0};
		xbox_emmc_read_csd(csd_raw);
		return pfc_send_frame(sock, PFC_MSG_RESPONSE, csd_raw, sizeof(csd_raw));
	}

	if (cmd.cmd == EMMC_GET_EXT_CSD) {
		maybe_stop_smc();
		uint8_t ext_csd[512];
		xbox_emmc_read_ext_csd(ext_csd);
		return pfc_send_frame(sock, PFC_MSG_RESPONSE, ext_csd, sizeof(ext_csd));
	}

	if (cmd.cmd == EMMC_READ) {
		maybe_stop_smc();
		uint8_t buffer[PFC_EMMC_BLOCK_BYTES];
		int ret = xbox_emmc_read_block((int)cmd.lba, buffer);
		if (ret == 0) {
			return pfc_send_u32_with_data(sock, (uint32_t)ret, buffer, sizeof(buffer));
		}
		return pfc_send_u32(sock, (uint32_t)ret);
	}

	if (cmd.cmd == EMMC_READ_STREAM) {
		maybe_stop_smc();
		uint32_t end = cmd.lba;
		for (uint32_t i = 0; i < end; i++) {
			uint8_t buffer[PFC_EMMC_BLOCK_BYTES];
			int ret = xbox_emmc_read_block((int)i, buffer);
			if (ret == 0) {
				if (pfc_send_u32_with_data(sock, (uint32_t)ret, buffer, sizeof(buffer))) {
					return -1;
				}
			} else {
				return pfc_send_u32(sock, (uint32_t)ret);
			}
		}
		return 0;
	}

	if (cmd.cmd == EMMC_WRITE) {
		maybe_stop_smc();
		if (extra_len != PFC_EMMC_BLOCK_BYTES) {
			return pfc_send_u32(sock, 0xFFFFFFFFu);
		}
		uint32_t ret = (uint32_t)xbox_emmc_write_block((int)cmd.lba, (uint8_t *)extra);
		return pfc_send_u32(sock, ret);
	}

	if (cmd.cmd == EMMC_WRITE_MULTI) {
		maybe_stop_smc();
		if (extra_len < 2) {
			return pfc_send_u32(sock, 0xFFFFFFFFu);
		}
		uint16_t count;
		memcpy(&count, extra, sizeof(count));
		const uint8_t *data = extra + 2;
		uint32_t data_len = extra_len - 2;
		if (count == 0) {
			return pfc_send_u32(sock, 0);
		}
		if (data_len != (uint32_t)count * PFC_EMMC_BLOCK_BYTES) {
			return pfc_send_u32(sock, 0xFFFFFFFFu);
		}
		for (uint16_t i = 0; i < count; i++) {
			const uint8_t *blk = data + (uint32_t)i * PFC_EMMC_BLOCK_BYTES;
			uint32_t ret = (uint32_t)xbox_emmc_write_block((int)(cmd.lba + i), (uint8_t *)blk);
			if (ret != 0) {
				uint32_t resp[2] = {ret, i};
				return pfc_send_bytes(sock, resp, sizeof(resp));
			}
		}
		{
			uint32_t resp[2] = {0, count};
			return pfc_send_bytes(sock, resp, sizeof(resp));
		}
	}

	return pfc_send_u32(sock, 0xFFFFFFFFu);
}

static void handle_client(int sock)
{
	uint8_t *payload = malloc(PFC_MAX_PAYLOAD);
	if (!payload) {
		return;
	}

	while (1) {
		pfc_hdr_t hdr;
		if (recv_all(sock, &hdr, sizeof(hdr))) {
			break;
		}

		if (hdr.magic != PFC_MAGIC || hdr.version != PFC_VERSION || hdr.type != PFC_MSG_REQUEST) {
			break;
		}

		if (hdr.length > PFC_MAX_PAYLOAD) {
			break;
		}

		if (hdr.length && recv_all(sock, payload, hdr.length)) {
			break;
		}

		if (handle_cmd(sock, payload, hdr.length)) {
			break;
		}
	}

	free(payload);

	if (xbox_smc_stopped) {
		xbox_start_smc();
	}
}

static void pfc_server_task(void *arg)
{
	(void)arg;

	int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (listen_sock < 0) {
		vTaskDelete(NULL);
	}

	int yes = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(PFC_TCP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(listen_sock);
		vTaskDelete(NULL);
	}

	if (listen(listen_sock, 1) != 0) {
		close(listen_sock);
		vTaskDelete(NULL);
	}

	ESP_LOGI(TAG, "listening on port %d", PFC_TCP_PORT);

	while (1) {
		struct sockaddr_in6 source_addr;
		socklen_t addr_len = sizeof(source_addr);
		int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
		if (sock < 0) {
			continue;
		}

		ESP_LOGI(TAG, "client connected");
		handle_client(sock);
		shutdown(sock, SHUT_RDWR);
		close(sock);
		ESP_LOGI(TAG, "client disconnected");
	}
}

void pfc_server_start(void)
{
	xTaskCreate(pfc_server_task, "pfc_server", 8192, NULL, 5, NULL);
}
