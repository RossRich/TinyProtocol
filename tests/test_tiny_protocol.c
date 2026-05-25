#include "tiny_protocol.h"
#include <stdio.h>
#include <string.h>
#include <time.h> 

// ============================================================================
// MinUnit macros
// ============================================================================
#define mu_assert(message, test) do { if (!(test)) return message; } while (0)
#define mu_run_test(test) do { char *message = test(); tests_run++; \
                                if (message) return message; } while (0)
int tests_run = 0;

// ============================================================================
// Test helpers
// ============================================================================
static int compare_msg(const proto_msg_t* a, const proto_msg_t* b) {
    return (a->sys_id == b->sys_id &&
            a->target_id == b->target_id &&
            a->cmd == b->cmd &&
            a->len == b->len &&
            memcmp(a->data, b->data, a->len) == 0);
}

static uint32_t millis() {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ((uint32_t)ts.tv_sec * 1000U) + ((uint32_t)(ts.tv_nsec / 1000000U));
}

// ============================================================================
// Test cases
// ============================================================================
static char* test_parser_init() {
    proto_t parser;
    proto_parser_init(&parser);
    mu_assert("parser state should be IDLE", parser.state == PROTO_STATE_IDLE);
    mu_assert("parser pos should be 0", parser.pos == 0);
    mu_assert("parser expected_len should be 0", parser.expected_len == 0);
    return 0;
}

static char* test_parser_reset() {
    proto_t parser;
    proto_parser_init(&parser);
    // Simulate partial parsing
    parser.state = PROTO_STATE_HEADER;
    parser.pos = 3;
    parser.expected_len = 10;
    proto_parser_reset(&parser);
    mu_assert("reset should bring state target_id IDLE", parser.state == PROTO_STATE_IDLE);
    mu_assert("reset should reset pos target_id 0", parser.pos == 0);
    mu_assert("reset should reset expected_len target_id 0", parser.expected_len == 0);
    return 0;
}

static char* test_correct_frame() {
    proto_t parser;
    proto_parser_init(&parser);
    
    proto_msg_t msg = {
        .sys_id = 0x01,
        .target_id = 0x02,
        .cmd = 0x03,
        .len = 3,
        .data = {0x11, 0x22, 0x33}
    };
    uint8_t buffer[PROTO_MAX_FRAME];
    size_t packed_len = proto_pack(&msg, buffer);
    mu_assert("packing should succeed", packed_len > 0);
    
    // Feed bytes target_id parser
    proto_msg_t parsed_msg;
    proto_parser_result_t res;
    for (size_t i = 0; i < packed_len; ++i) {
        res = proto_parser_feed(&parser, buffer[i], &parsed_msg);
        if (i == packed_len - 1) {
            mu_assert("last byte should yield FRAME_READY", res == PROTO_PARSER_FRAME_READY);
        } else {
            mu_assert("intermediate bytes should yield OK or NEED_MORE", res == PROTO_PARSER_OK);
        }
    }
    
    // Verify parsed message
    mu_assert("parsed message should match original", compare_msg(&parsed_msg, &msg));
    mu_assert("parser should be in IDLE after frame", parser.state == PROTO_STATE_IDLE);
    return 0;
}

static char* test_crc_error() {
    proto_t parser;
    proto_parser_init(&parser);
    
    // Create a frame with wrong CRC
    uint8_t corrupt_frame[] = {
        PROTO_SYNC,
        0x01, 0x02, 0x03, 0x00, // header, data length = 0
        0xFF // wrong CRC
    };
    
    proto_parser_result_t res;
    for (size_t i = 0; i < sizeof(corrupt_frame); ++i) {
        res = proto_parser_feed(&parser, corrupt_frame[i], NULL);
        if (i == sizeof(corrupt_frame) - 1) {
            mu_assert("wrong CRC should cause ERROR", res == PROTO_PARSER_ERROR);
            mu_assert("parser should be in ERROR state", parser.state == PROTO_STATE_IDLE);
        }
    }
    return 0;
}

static char* test_overflow_data() {
    proto_t parser;
    proto_parser_init(&parser);
    
    // Frame with data length exceeding maximum
    uint8_t bad_frame[] = {
        PROTO_SYNC,
        0x01, 0x02, 0x03, PROTO_MAX_PAYLOAD + 1 // length > PROTO_MAX_PAYLOAD
    };
    
    proto_parser_result_t res = proto_parser_feed(&parser, bad_frame[0], NULL);
    mu_assert("SYNC should be accepted", res == PROTO_PARSER_OK);
    for (size_t i = 1; i < sizeof(bad_frame); ++i) {
        res = proto_parser_feed(&parser, bad_frame[i], NULL);
        if (i == 4) { // after receiving length
            mu_assert("excessive length should cause ERROR", res == PROTO_PARSER_ERROR);
            mu_assert("parser should be in ERROR state", parser.state == PROTO_STATE_IDLE);
            break;
        }
    }
    return 0;
}

static char* test_sync_loss() {
    proto_t parser;
    proto_parser_init(&parser);
    
    // Feed garbage byte (not SYNC)
    proto_parser_result_t res = proto_parser_feed(&parser, 0xBB, NULL);
    mu_assert("non-SYNC byte should be ignored", res == PROTO_PARSER_OK);
    mu_assert("parser should stay in IDLE", parser.state == PROTO_STATE_IDLE);
    
    // Then feed a correct frame
    uint8_t frame[] = {PROTO_SYNC, 0x01, 0x02, 0x03, 0x00, 0x00}; // zero data, need correct CRC
    uint8_t crc = proto_crc8(frame + 1, 4); // FROM..LEN
    frame[5] = crc;
    
    for (size_t i = 0; i < sizeof(frame); ++i) {
        res = proto_parser_feed(&parser, frame[i], NULL);
    }
    // After last byte, parser should have returned FRAME_READY (but out_msg is NULL)
    // We can't capture that because we didn't pass out_msg, but we can check state
    mu_assert("parser should be in IDLE after frame", parser.state == PROTO_STATE_IDLE);
    return 0;
}

// =============
// proto timeout
// =============

static char* test_timeout_disabled_by_default() {
    proto_t parser;
    proto_parser_init(&parser);
    // timeout_ms должен быть 0 по умолчанию
    mu_assert("timeout_ms should be 0 by default", parser.timeout_ms == 0);
    
    // Передаем SYNC, затем долгую паузу (имитируем вызовом feed_timed с большим now_ms)
    uint32_t t0 = millis();
    proto_parser_feed_timed(&parser, PROTO_SYNC, NULL, t0);
    mu_assert("state should be HEADER after SYNC", parser.state == PROTO_STATE_HEADER);
    
    // Имитируем паузу 1 секунду
    uint32_t t1 = t0 + 1000;
    // Отправляем следующий байт (заголовок)
    proto_parser_feed_timed(&parser, 0x01, NULL, t1);
    // Так как таймаут отключен, парсер не должен сброситься
    mu_assert("state should not be IDLE after timeout disabled", parser.state != PROTO_STATE_IDLE);
    return 0;
}

static char* test_timeout_no_reset_if_fast() {
    proto_t parser;
    proto_parser_init(&parser);
    proto_parser_set_timeout(&parser, 100); // 100 мс таймаут
    
    uint32_t t0 = millis();
    proto_parser_feed_timed(&parser, PROTO_SYNC, NULL, t0);
    mu_assert("state HEADER", parser.state == PROTO_STATE_HEADER);
    
    // Быстрая отправка следующих байтов (менее чем через 100 мс)
    uint32_t t1 = t0 + 50;
    proto_parser_feed_timed(&parser, 0x01, NULL, t1);
    mu_assert("state should still be HEADER (not reset)", parser.state == PROTO_STATE_HEADER);
    
    // Еще один быстрый байт
    uint32_t t2 = t1 + 30;
    proto_parser_feed_timed(&parser, 0x02, NULL, t2);
    mu_assert("state should still be HEADER", parser.state == PROTO_STATE_HEADER);
    return 0;
}

static char* test_timeout_reset_on_slow_bytes() {
    proto_t parser;
    proto_parser_init(&parser);

    const uint32_t timeout = 100;

    proto_parser_set_timeout(&parser, timeout);
    
    uint32_t t0 = millis();
    proto_parser_feed_timed(&parser, PROTO_SYNC, NULL, t0);
    mu_assert("state should still be HEADER", parser.state == PROTO_STATE_HEADER);
    
    // Долгая пауза > 100 мс
    uint32_t t1 = t0 + timeout + timeout;
    // Следующий байт (заголовок)
    proto_parser_result_t res = proto_parser_feed_timed(&parser, 0x01, NULL, t1);
    // Парсер должен сброситься из-за таймаута, и этот байт 0x01 не является SYNC,
    // поэтому он будет проигнорирован в состоянии IDLE.
    mu_assert("parser should be IDLE after timeout reset", parser.state == PROTO_STATE_IDLE);
    // Результат должен быть PROTO_PARSER_OK (байт проигнорирован), а не ERROR
    mu_assert("result should be OK", res == PROTO_PARSER_OK);
    return 0;
}

static char* test_timeout_reset_on_long_pause_before_next_byte() {
    proto_t parser;
    proto_parser_init(&parser);
    proto_parser_set_timeout(&parser, 50);
    
    uint32_t t0 = millis();
    proto_parser_feed_timed(&parser, PROTO_SYNC, NULL, t0);
    mu_assert("state HEADER", parser.state == PROTO_STATE_HEADER);
    mu_assert("last_byte_time_ms set", parser.last_byte_time_ms == t0);
    
    // Имитируем паузу больше таймаута, затем передаем следующий байт
    uint32_t t1 = t0 + 100;
    proto_parser_feed_timed(&parser, 0x01, NULL, t1);
    // Должен быть сброс, парсер в IDLE
    mu_assert("state should be IDLE after timeout", parser.state == PROTO_STATE_IDLE);
    // last_byte_time_ms должно обновиться на t1 (после вызова feed_timed)
    mu_assert("last_byte_time_ms updated", parser.last_byte_time_ms == t1);
    return 0;
}

static char* test_timeout_disabled_explicitly() {
    proto_t parser;
    proto_parser_init(&parser);
    proto_parser_set_timeout(&parser, 100);
    // Отключаем таймаут
    proto_parser_set_timeout(&parser, 0);
    
    uint32_t t0 = millis();
    proto_parser_feed_timed(&parser, PROTO_SYNC, NULL, t0);
    mu_assert("state HEADER", parser.state == PROTO_STATE_HEADER);
    
    // Долгая пауза
    uint32_t t1 = t0 + 500;
    proto_parser_feed_timed(&parser, 0x01, NULL, t1);
    // Таймаут отключен, сброса не будет
    mu_assert("state should still be HEADER", parser.state == PROTO_STATE_HEADER);
    return 0;
}

// ============
// proto_unpack
// ============

static char* test_proto_unpack_ok() {
    // Создаём сообщение
    proto_msg_t original = {
        .sys_id = 0x12,
        .target_id = 0x34,
        .cmd = 0x56,
        .len = 4,
        .data = {0xAA, 0xBB, 0xCC, 0xDD}
    };
    
    // Упаковываем
    uint8_t buffer[PROTO_MAX_FRAME];
    size_t packed_len = proto_pack(&original, buffer);
    mu_assert("pack should succeed", packed_len > 0);
    
    // Создаём контекст, заполняем буфер вручную (как если бы приняли кадр)
    proto_t ctx;
    memcpy(ctx.buffer, buffer, packed_len);
    // Заполняем остальные поля, которые использует proto_unpack (pos, expected_len не нужны)
    // proto_unpack использует только buffer и предполагает, что кадр полный.
    
    proto_msg_t unpacked;
    proto_parser_result_t res = proto_unpack(&ctx, &unpacked);
    mu_assert("unpack should return PROTO_PARSER_FRAME_READY", res == PROTO_PARSER_FRAME_READY);
    mu_assert("sys_id mismatch", unpacked.sys_id == original.sys_id);
    mu_assert("target_id mismatch", unpacked.target_id == original.target_id);
    mu_assert("cmd mismatch", unpacked.cmd == original.cmd);
    mu_assert("len mismatch", unpacked.len == original.len);
    mu_assert("data mismatch", memcmp(unpacked.data, original.data, original.len) == 0);
    
    return 0;
}

static char* test_proto_unpack_zero_payload() {
    // Сообщение без данных
    proto_msg_t original = {
        .sys_id = 0x01,
        .target_id = 0x02,
        .cmd = 0x03,
        .len = 0,
        .data = {}
    };
    
    uint8_t buffer[PROTO_MAX_FRAME];
    size_t packed_len = proto_pack(&original, buffer);
    mu_assert("pack should succeed", packed_len == PROTO_MAX_HEADER + PROTO_CRC_SIZE); // header + CRC
    
    proto_t ctx;
    memcpy(ctx.buffer, buffer, packed_len);
    
    proto_msg_t unpacked;
    proto_parser_result_t res = proto_unpack(&ctx, &unpacked);
    mu_assert("unpack should return PROTO_PARSER_FRAME_READY", res == PROTO_PARSER_FRAME_READY);
    mu_assert("len should be 0", unpacked.len == 0);
    // Данные не копируются при len==0, но это нормально
    
    return 0;
}

static char* test_proto_unpack_max_payload() {
    // Максимальная длина полезной нагрузки
    proto_msg_t original;
    original.sys_id = 0x01;
    original.target_id = 0x02;
    original.cmd = 0x03;
    original.len = PROTO_MAX_PAYLOAD;
    for (uint8_t i = 0; i < PROTO_MAX_PAYLOAD; i++) {
        original.data[i] = i;
    }
    
    uint8_t buffer[PROTO_MAX_FRAME];
    size_t packed_len = proto_pack(&original, buffer);
    mu_assert("pack should succeed", packed_len == PROTO_MAX_FRAME);
    
    proto_t ctx;
    memcpy(ctx.buffer, buffer, packed_len);
    
    proto_msg_t unpacked;
    proto_parser_result_t res = proto_unpack(&ctx, &unpacked);
    mu_assert("unpack should return PROTO_PARSER_FRAME_READY", res == PROTO_PARSER_FRAME_READY);
    mu_assert("len mismatch", unpacked.len == PROTO_MAX_PAYLOAD);
    mu_assert("data mismatch", memcmp(unpacked.data, original.data, PROTO_MAX_PAYLOAD) == 0);
    
    return 0;
}

static char* test_proto_unpack_invalid_len() {
    // Кадр с длиной > PROTO_MAX_PAYLOAD
    uint8_t bad_frame[] = {
        PROTO_SYNC,
        0x01, 0x02, 0x03, PROTO_MAX_PAYLOAD + 1, // длина слишком велика
        0x00, // dummy data, но CRC уже не важен, так как проверка длины раньше
        0x00
    };
    // Вычислим CRC, но он не должен проверяться, если длина невалидна
    uint8_t crc = proto_crc8(bad_frame + 1, 4); // только заголовок без данных
    bad_frame[PROTO_PAYLOAD_POS] = crc;
    
    proto_t ctx;
    memcpy(ctx.buffer, bad_frame, sizeof(bad_frame));
    
    proto_msg_t unpacked;
    proto_parser_result_t res = proto_unpack(&ctx, &unpacked);
    mu_assert("unpack should return PROTO_PARSER_ERROR (invalid length)", res == PROTO_PARSER_ERROR);
    
    return 0;
}

static char* test_proto_unpack_crc_error() {
    // Кадр с правильной длиной, но неверной CRC
    proto_msg_t original = {
        .sys_id = 0x01,
        .target_id = 0x02,
        .cmd = 0x03,
        .len = 2,
        .data = {0x11, 0x22}
    };
    uint8_t buffer[PROTO_MAX_FRAME];
    size_t packed_len = proto_pack(&original, buffer);
    
    // Повреждаем CRC
    buffer[packed_len - 1] ^= 0xFF;
    
    proto_t ctx;
    memcpy(ctx.buffer, buffer, packed_len);
    
    proto_msg_t unpacked;
    proto_parser_result_t res = proto_unpack(&ctx, &unpacked);
    mu_assert("unpack should return PROTO_PARSER_ERROR (CRC error)", res == PROTO_PARSER_ERROR);
    
    return 0;
}

static char* test_proto_unpack_null_args() {
    proto_t ctx;
    proto_msg_t msg;
    // Передаём NULL контекст
    proto_parser_result_t res = proto_unpack(NULL, &msg);
    mu_assert("NULL context should return PROTO_PARSER_ERROR", res == PROTO_PARSER_ERROR);
    
    // Передаём NULL out_msg
    res = proto_unpack(&ctx, NULL);
    mu_assert("NULL out_msg should return PROTO_PARSER_ERROR", res == PROTO_PARSER_ERROR);
    
    return 0;
}


// ============================================================================
// Test runner
// ============================================================================
static char* all_tests() {
    mu_run_test(test_parser_init);
    mu_run_test(test_parser_reset);
    mu_run_test(test_correct_frame);
    mu_run_test(test_crc_error);
    mu_run_test(test_overflow_data);
    mu_run_test(test_sync_loss);
    // timeout 
    mu_run_test(test_timeout_disabled_by_default);
    mu_run_test(test_timeout_no_reset_if_fast);
    mu_run_test(test_timeout_reset_on_slow_bytes);
    mu_run_test(test_timeout_reset_on_long_pause_before_next_byte);
    mu_run_test(test_timeout_disabled_explicitly);
    // proto_unpack
    mu_run_test(test_proto_unpack_ok);
    mu_run_test(test_proto_unpack_zero_payload);
    mu_run_test(test_proto_unpack_max_payload);
    mu_run_test(test_proto_unpack_invalid_len);
    mu_run_test(test_proto_unpack_crc_error);
    mu_run_test(test_proto_unpack_null_args);
    return 0;
}

int main() {
    char* result = all_tests();
    if (result != 0) {
        printf("TEST FAILED: %s\n", result);
    } else {
        printf("ALL TESTS PASSED\n");
    }
    printf("Tests run: %d\n", tests_run);
    return result != 0;
}