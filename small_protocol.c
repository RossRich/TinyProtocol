#include "protocol.h"
#include <string.h>

// CRC-8 (Dallas/Maxim, полином 0x31)
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

// Упаковка: логическое сообщение → байты для UART
// Возвращает: длину пакета (>0) или -1 при ошибке
int proto_pack(const proto_msg_t *msg, uint8_t *out_buf) {
    if (!msg || !out_buf || msg->len > PROTO_MAX_DATA) return -1;

    out_buf[0] = PROTO_SYNC;
    out_buf[1] = msg->from;
    out_buf[2] = msg->to;
    out_buf[3] = msg->cmd;
    out_buf[4] = msg->len;
    memcpy(out_buf + 5, msg->data, msg->len);
    out_buf[5 + msg->len] = proto_crc8(out_buf + 1, 4 + msg->len); // CRC по FROM..DATA

    return 5 + msg->len + 1; // sync + header + data + crc
}

// Распаковка: байты из UART → логическое сообщение
// Возвращает: 0 (OK), -1 (формат/длина), -2 (CRC)
int proto_unpack(const proto_t* context, proto_msg_t* out_msg) {
    if (!context || !out_msg) {
        return -1;  
    } 

    const uint8_t user_data_len = context->header[PROTO_LEN_POS];
    if (user_data_len > PROTO_MAX_DATA) {
        return -1;
    }

    const uint8_t* data_buf = (const uint8_t *)context;  

    const uint8_t crc = proto_crc8(data_buf, PROTO_MAX_HEADER + user_data_len);
    if (context->crc != crc) {
        return -2;
    }

    uint8_t *msg_buf = (uint8_t*)out_msg;
    memcpy(msg_buf, data_buf, sizeof(proto_msg_t));

    return 0;
}