#ifndef _SMALL_PROTOCOL_H_
#define _SMALL_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

// [SYNC][SYS_ID][TARGET_ID][CMD][LEN][DATA...][CRC]

// --- Константы ---
#define PROTO_SYNC 0xAA
#define PROTO_MAX_FRAME 32U
#define PROTO_MAX_HEADER 4U  // SYS_ID | TARGET_ID | CMD | LEN
#define PROTO_MAX_DATA 26U   // PROTO_MAX_FRAME - 1(sync) - 4(meta) - 1(CRC)

#define PROTO_SYNK_POS 0U
#define PROTO_SYS_ID_POS 1U
#define PROTO_TARGET_ID_POS 2U
#define PROTO_CMD_POS 3U
#define PROTO_LEN_POS 4U
#define PROTO_DATA_POS 5U

// --- Адресация ---
#define ADDR_BROADCAST 0xFF
#define ADDR_MASTER 0x01

typedef uint8_t proto_cmd_t;

// --- Логическое сообщение (в RAM) ---
typedef struct {
  uint8_t from;
  uint8_t to;
  proto_cmd_t cmd;
  uint8_t len;
  uint8_t data[PROTO_MAX_DATA];
} proto_msg_t;

typedef struct {
  uint8_t header[PROTO_MAX_HEADER];
  uint8_t data[PROTO_MAX_DATA];
  uint8_t crc;
} proto_t;

#ifdef __cplusplus
extern "C" {
#endif

// --- API ---
uint8_t proto_crc8(const uint8_t* buf, size_t len);
int proto_pack(const proto_msg_t* msg, uint8_t* out_buf);
int proto_unpack(const proto_t* context, proto_msg_t* out_msg);

#ifdef __cplusplus
}
#endif

// --- Удобные макросы ---
#define PROTO_MSG_INIT(from, to, cmd, data_ptr, data_len)            \
  (proto_msg_t) {                                                    \
    .from = (from), .to = (to), .cmd = (cmd),                        \
    .len = (data_len) > PROTO_MAX_DATA ? PROTO_MAX_DATA : (data_len) \
  }

#endif