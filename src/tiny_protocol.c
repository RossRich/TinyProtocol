/*
  MIT License
  Copyright (c) 2026 Богдан Лещенко
 */

#include "tiny_protocol.h"
#include <string.h>

uint8_t proto_crc8(const uint8_t *buf, size_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *buf++;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
  }
  return crc;
}

size_t proto_pack(const proto_msg_t *msg, uint8_t *out_buf) {
  if (!msg || !out_buf || (msg->len > PROTO_MAX_PAYLOAD)) {
    return 0;
  }

  uint8_t idx = 0;
  out_buf[idx++] = PROTO_SYNC;
  out_buf[idx++] = msg->len;
  out_buf[idx++] = msg->sys_id;
  out_buf[idx++] = msg->target_id;
  out_buf[idx++] = msg->msg_id;
  memcpy(out_buf + PROTO_PAYLOAD_POS, msg->data, msg->len);
  out_buf[PROTO_PAYLOAD_POS + msg->len] = proto_crc8(out_buf + 1, (PROTO_PAYLOAD_POS - 1) + msg->len); // CRC по LEN..DATA

  return PROTO_MAX_HEADER + msg->len + PROTO_CRC_SIZE; // header + data + crc
}

proto_parser_result_t proto_unpack(const proto_t *context, proto_msg_t *out_msg) {
  if (!context || !out_msg) {
    return PROTO_PARSER_ERROR;
  }

  const uint8_t msg_payload_len = context->buffer[PROTO_LEN_POS];
  if (msg_payload_len > PROTO_MAX_PAYLOAD) {
    return PROTO_PARSER_ERROR;
  }

  const uint8_t *start_msg = context->buffer + 1;                              // исключаем sync
  const uint8_t msg_len = PROTO_MAX_HEADER + msg_payload_len - PROTO_CRC_SIZE; // длина заголовка + длина данных - crc
  const uint8_t crc = proto_crc8(start_msg, msg_len);
  const uint8_t msg_crc_pos = PROTO_PAYLOAD_POS + msg_payload_len;
  if (context->buffer[msg_crc_pos] != crc) {
    return PROTO_PARSER_ERROR;
  }

  out_msg->sys_id = context->buffer[PROTO_SYS_ID_POS];
  out_msg->target_id = context->buffer[PROTO_TARGET_ID_POS];
  out_msg->msg_id = context->buffer[PROTO_MSG_ID_POS];
  out_msg->len = context->buffer[PROTO_LEN_POS];
  memcpy(out_msg->data, context->buffer + PROTO_PAYLOAD_POS, msg_payload_len);

  return PROTO_PARSER_FRAME_READY;
}

void proto_parser_reset(proto_t *context) {
  if (!context) {
    return;
  }

  context->state = PROTO_STATE_IDLE;
  context->pos = 0;
  context->expected_len = 0;
  context->last_byte_time_ms = 0;
  context->buffer[0] = 0;
}

void proto_parser_init(proto_t *context) {
  if (!context) {
    return;
  }

  context->timeout_ms = 0;
  proto_parser_reset(context);
}

void proto_parser_set_timeout(proto_t *ctx, uint32_t timeout_ms) {
  if (ctx) {
    if (ctx->state != PROTO_STATE_IDLE) {
      proto_parser_reset(ctx);
    }

    ctx->timeout_ms = timeout_ms;
  }
}

proto_parser_result_t proto_parser_feed(proto_t *context, uint8_t byte, proto_msg_t *out_msg) {
  if (!context) {
    return PROTO_PARSER_ERROR;
  }

  proto_parser_result_t parse_res = PROTO_PARSER_OK;

  switch (context->state) {
  case PROTO_STATE_IDLE:
    if (byte == PROTO_SYNC) {
      proto_parser_reset(context);
      context->buffer[0] = byte;
      context->pos = 1;
      context->state = PROTO_STATE_HEADER;
    }
    break;

  case PROTO_STATE_HEADER:
    context->buffer[context->pos++] = byte;
    if (context->pos == PROTO_PAYLOAD_POS) {
      // Все байты заголовка получены, извлекаем LEN
      context->expected_len = context->buffer[PROTO_LEN_POS];
      if (context->expected_len > PROTO_MAX_PAYLOAD) {
        context->state = PROTO_STATE_IDLE;
        parse_res = PROTO_PARSER_ERROR;
      } else if (context->expected_len == 0) {
        // Нет данных, переходим к ожиданию CRC
        context->state = PROTO_STATE_CRC;
      } else {
        context->state = PROTO_STATE_DATA;
      }
    }
    break;

  case PROTO_STATE_DATA:
    context->buffer[context->pos++] = byte;
    if (context->pos == (PROTO_PAYLOAD_POS + context->expected_len)) {
      context->state = PROTO_STATE_CRC;
    }
    break;

  case PROTO_STATE_CRC:
    context->buffer[context->pos++] = byte;
    context->state = PROTO_STATE_IDLE;
    parse_res = proto_unpack(context, out_msg);
    break;

  default:
    context->state = PROTO_STATE_IDLE;
    parse_res = PROTO_PARSER_ERROR;
  }

  return parse_res;
}

proto_parser_result_t proto_parser_feed_timed(proto_t *context, uint8_t byte, proto_msg_t *out_msg, uint32_t now_ms) {
  if (!context) {
    return PROTO_PARSER_ERROR;
  }

  proto_parser_result_t parser_res = PROTO_PARSER_ERROR;

  if (context->timeout_ms > 0) {
    // Если таймаут включен
    const uint32_t dt = now_ms - context->last_byte_time_ms;

    if (dt > context->timeout_ms && context->state != PROTO_STATE_IDLE) {
      proto_parser_reset(context);
    }

    // Cначала вызов парсера, так как после найденного SYNC будет сброс и времени в том числе.
    parser_res = proto_parser_feed(context, byte, out_msg);

    // Гарантированно сохраняем время даже если был сброс в IDLE
    context->last_byte_time_ms = now_ms;
  } else {
    parser_res = proto_parser_feed(context, byte, out_msg);
  }

  return parser_res;
}