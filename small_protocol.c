#include "small_protocol.h"
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

  return 1;
}

// ============================================================================
// Парсер потока
// ============================================================================

void proto_parser_init(proto_parser_t* parser) {
    if (!parser) return;
    parser->state = PROTO_STATE_IDLE;
    parser->pos = 0;
    parser->expected_len = 0;
    parser->timeout_counter = 0;
    // Буфер можно не очищать, но для безопасности обнулим первый байт
    parser->buffer[0] = 0;
}

void proto_parser_reset(proto_parser_t* parser) {
    if (!parser) return;
    parser->state = PROTO_STATE_IDLE;
    parser->pos = 0;
    parser->expected_len = 0;
    parser->timeout_counter = 0;
}

proto_parser_result_t proto_parser_feed(proto_parser_t* parser, uint8_t byte, proto_msg_t* out_msg) {
    if (!parser) return PROTO_PARSER_ERROR;

    switch (parser->state) {
        case PROTO_STATE_IDLE:
            if (byte == PROTO_SYNC) {
                parser->buffer[0] = byte;
                parser->pos = 1;
                parser->state = PROTO_STATE_HEADER;
            }
            return PROTO_PARSER_OK;

        case PROTO_STATE_HEADER:
            // Накопление заголовка (FROM, TO, CMD, LEN)
            // Заголовок занимает байты с PROTO_SYS_ID_POS (1) до PROTO_LEN_POS (4)
            // Всего 4 байта
            if (parser->pos < PROTO_DATA_POS) { // DATA_POS = 5, значит позиции 1-4
                parser->buffer[parser->pos++] = byte;
                if (parser->pos == PROTO_DATA_POS) {
                    // Все байты заголовка получены, извлекаем LEN
                    parser->expected_len = parser->buffer[PROTO_LEN_POS];
                    if (parser->expected_len > PROTO_MAX_DATA) {
                        parser->state = PROTO_STATE_ERROR;
                        return PROTO_PARSER_ERROR;
                    }
                    if (parser->expected_len == 0) {
                        // Нет данных, переходим к ожиданию CRC
                        parser->state = PROTO_STATE_CRC;
                    } else {
                        parser->state = PROTO_STATE_DATA;
                    }
                }
            }
            return PROTO_PARSER_OK;

        case PROTO_STATE_DATA:
            // Накопление данных
            if (parser->pos < PROTO_DATA_POS + parser->expected_len) {
                parser->buffer[parser->pos++] = byte;
                if (parser->pos == PROTO_DATA_POS + parser->expected_len) {
                    // Все данные получены, ожидаем CRC
                    parser->state = PROTO_STATE_CRC;
                }
            }
            return PROTO_PARSER_OK;

        case PROTO_STATE_CRC:
            // Приём CRC
            parser->buffer[parser->pos++] = byte;
            // Теперь кадр полностью получен
            parser->state = PROTO_STATE_COMPLETE;
            // Проверяем CRC
            // CRC вычисляется от байтов с позиции PROTO_SYS_ID_POS (1) до позиции pos-2 (данные)
            // В буфере: [0]=SYNC, [1]=FROM, [2]=TO, [3]=CMD, [4]=LEN, [5..]=data, [pos-1]=CRC
            uint8_t crc_calc = proto_crc8(parser->buffer + PROTO_SYS_ID_POS, parser->pos - 1 - PROTO_SYS_ID_POS);
            uint8_t crc_received = parser->buffer[parser->pos - 1];
            if (crc_calc != crc_received) {
                parser->state = PROTO_STATE_ERROR;
                return PROTO_PARSER_ERROR;
            }
            // Кадр корректен, заполняем out_msg если передан
            if (out_msg) {
                out_msg->from = parser->buffer[PROTO_SYS_ID_POS];
                out_msg->to = parser->buffer[PROTO_TARGET_ID_POS];
                out_msg->cmd = parser->buffer[PROTO_CMD_POS];
                out_msg->len = parser->buffer[PROTO_LEN_POS];
                if (parser->expected_len > 0) {
                    memcpy(out_msg->data, parser->buffer + PROTO_DATA_POS, parser->expected_len);
                }
            }
            parser->state = PROTO_STATE_IDLE; // автоматический сброс для следующего кадра
            return PROTO_PARSER_FRAME_READY;

        case PROTO_STATE_COMPLETE:
            // Это состояние должно быть кратковременным, после него сбрасываем в IDLE
            parser->state = PROTO_STATE_IDLE;
            return PROTO_PARSER_OK;

        case PROTO_STATE_ERROR:
            // Остаёмся в состоянии ошибки до явного сброса
            return PROTO_PARSER_ERROR;

        default:
            parser->state = PROTO_STATE_ERROR;
            return PROTO_PARSER_ERROR;
    }
}

size_t proto_parser_feed_batch(proto_parser_t* parser, const uint8_t* data, size_t len, proto_msg_t* out_msg) {
    if (!parser || !data) return 0;

    size_t i;
    for (i = 0; i < len; ++i) {
        proto_parser_result_t res = proto_parser_feed(parser, data[i], out_msg);
        if (res == PROTO_PARSER_FRAME_READY) {
            // Кадр готов, возвращаем количество обработанных байт (включая текущий)
            return i + 1;
        }
        if (res == PROTO_PARSER_ERROR) {
            // Ошибка, возвращаем количество обработанных байт до ошибки
            return i + 1;
        }
        // Продолжаем обработку
    }
    // Все байты обработаны, кадр не готов
    return i;
}