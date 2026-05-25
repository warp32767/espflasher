#ifndef PFC_PROTO_H
#define PFC_PROTO_H

#include <stdint.h>

#define PFC_MAGIC 0x50464331u
#define PFC_VERSION 0x0001u

enum pfc_msg_type
{
	PFC_MSG_REQUEST = 0,
	PFC_MSG_RESPONSE = 1,
};

typedef struct __attribute__((packed)) pfc_hdr
{
	uint32_t magic;
	uint16_t version;
	uint16_t type;
	uint32_t length;
} pfc_hdr_t;

typedef struct __attribute__((packed)) pfc_cmd
{
	uint8_t cmd;
	uint32_t lba;
} pfc_cmd_t;

#endif
