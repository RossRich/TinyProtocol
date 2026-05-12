#include "small_protocol.h"
#include <stdio.h>
#include <string.h>

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
            mu_assert("intermediate bytes should yield OK or NEED_MORE",
                      res == PROTO_PARSER_OK || res == PROTO_PARSER_NEED_MORE);
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