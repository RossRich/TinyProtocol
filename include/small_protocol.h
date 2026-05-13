#ifndef _SMALL_PROTOCOL_H_
#define _SMALL_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#define PROTO_SYNC 0xAA
#define PROTO_MAX_FRAME 32U
#define PROTO_MAX_HEADER 5U   // SYNC | SYS_ID | TARGET_ID | CMD | LEN
#define PROTO_MAX_PAYLOAD 26U // PROTO_MAX_FRAME - 1(sync) - 4(meta) - 1(CRC)

// [SYNC][SYS_ID][TARGET_ID][CMD][LEN][DATA...][CRC]
#define PROTO_SYNC_POS 0U
#define PROTO_SYS_ID_POS 1U
#define PROTO_TARGET_ID_POS 2U
#define PROTO_CMD_POS 3U
#define PROTO_LEN_POS 4U
#define PROTO_PAYLOAD_POS 5U

#define ADDR_BROADCAST 0xFF

typedef uint8_t proto_cmd_t;

typedef struct {
  uint8_t sys_id;
  uint8_t target_id;
  proto_cmd_t cmd;
  uint8_t len;
  uint8_t data[PROTO_MAX_PAYLOAD];
} proto_msg_t;

// Парсер потока (конечный автомат)
typedef enum {
  PROTO_STATE_IDLE,   // Ожидание SYNC
  PROTO_STATE_HEADER, // Чтение заголовка (FROM, TO, CMD, LEN)
  PROTO_STATE_DATA,   // Чтение данных
  PROTO_STATE_CRC,    // Чтение CRC
} proto_parser_state_t;

// Результат обработки байта
typedef enum {
  PROTO_PARSER_OK,          // Байт обработан, продолжать
  PROTO_PARSER_FRAME_READY, // Кадр полностью принят и распарсен
  PROTO_PARSER_ERROR,       // Ошибка (CRC, формат, переполнение)
} proto_parser_result_t;

// Контекст парсера
typedef struct {
  proto_parser_state_t state;      // Текущее состояние
  uint8_t buffer[PROTO_MAX_FRAME]; // Буфер для накопления кадра (включая SYNC)
  uint8_t pos;                     // Текущая позиция в буфере
  uint8_t expected_len;            // Ожидаемая длина данных (из поля LEN)
  uint32_t last_byte_time_ms;      // Таймер ожидания входящих байт до сброса
  uint32_t timeout_ms;             // Максимальное ожидание очередного байта если SYNC был найден
} proto_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализирует парсер перед началом работы.
 * @param context Указатель на структуру парсера.
 * @note Приводит парсер в состояние IDLE и обнуляет внутренние счётчики.
 */
void proto_parser_init(proto_t *context);

/**
 * @brief Сбрасывает парсер в состояние IDLE.
 * @param context Указатель на структуру парсера.
 * @details Сброс позволяет повторно использовать парсер после ошибки или успешного приёма кадра.
 */
void proto_parser_reset(proto_t *context);

/**
 * @brief Задать максимальное время ожидания очередного байта перед сбросом в исходное состояние
 * @param ctx Указатель на структуру парсера
 * @param timeout_ms Максимальное время ожидания
 * @note Необходимо вызывать функцию proto_parser_feed_timed(...)
 */
void proto_parser_set_timeout(proto_t *ctx, uint32_t timeout_ms);

/**
 * @brief Обрабатывает один байт из входного потока.
 * @param context Указатель на структуру парсера.
 * @param byte Принятый байт.
 * @param out_msg Указатель на структуру сообщения для заполнения (может быть NULL).
 * @return Результат обработки:
 *   - PROTO_PARSER_OK: байт обработан, кадр ещё не готов.
 *   - PROTO_PARSER_FRAME_READY: кадр полностью принят и распарсен, out_msg заполнен.
 *   - PROTO_PARSER_ERROR: ошибка (CRC, формат, переполнение).
 *   - PROTO_PARSER_NEED_MORE: ожидание дополнительных данных (промежуточное состояние).
 * @note Если out_msg != NULL и возвращено PROTO_PARSER_FRAME_READY, структура out_msg содержит распарсенное сообщение.
 */
proto_parser_result_t proto_parser_feed(proto_t *context, uint8_t byte, proto_msg_t *out_msg);

/**
 * @brief Обрабатывает один байт из входного потока учитывая задержку между поступлением байт
 * @param context Указатель на структуру парсера
 * @param byte Принятый байт
 * @param out_msg Указатель на структуру сообщения для заполнения (может быть NULL)
 * @param now_ms Текущее время системы, миллисекунд
 * @return Результат обработки:
 *   - PROTO_PARSER_OK: байт обработан, кадр ещё не готов.
 *   - PROTO_PARSER_FRAME_READY: кадр полностью принят и распарсен, out_msg заполнен.
 *   - PROTO_PARSER_ERROR: ошибка (CRC, формат, переполнение).
 *   - PROTO_PARSER_NEED_MORE: ожидание дополнительных данных (промежуточное состояние).
 * @note Если не задать максимальное время меджу чтением байт, то парсер будет игнорировать обработку времени, что идентично
 * вызову обычной функции proto_parser_feed(...). Если out_msg != NULL и возвращено PROTO_PARSER_FRAME_READY, структура out_msg
 * содержит распарсенное сообщение.
 */
proto_parser_result_t proto_parser_feed_timed(proto_t *context, uint8_t byte, proto_msg_t *out_msg, uint32_t now_ms);

/**
 * @brief Вычисляет CRC-8 (Dallas/Maxim, полином 0x31).
 * @param buf Указатель на данные.
 * @param len Длина данных в байтах.
 * @return Значение CRC-8.
 */
uint8_t proto_crc8(const uint8_t *buf, size_t len);

/**
 * @brief Упаковывает логическое сообщение в байтовый кадр для передачи по UART.
 * @param msg Указатель на структуру сообщения.
 * @param out_buf Буфер для записи кадра (должен иметь размер не менее PROTO_MAX_FRAME).
 * @return Кол-во байт записанных в буфер
 */
size_t proto_pack(const proto_msg_t *msg, uint8_t *out_buf);

/**
 * @brief Распаковывает байтовый кадр в логическое сообщение.
 * @param context Указатель на структуру кадра (заголовок, данные, CRC).
 * @param out_msg Указатель на структуру сообщения для заполнения.
 * @return Результат распаковки:
 *    1: OK (кадр корректен).
 *    -1: ошибка формата или длины.
 *    -2: ошибка CRC.
 */
int8_t proto_unpack(const proto_t *context, proto_msg_t *out_msg);

#ifdef __cplusplus
}
#endif

#endif