# Small Protocol Library

Библиотека для работы с простым UART-протоколом в embedded-системах. Реализует упаковку/распаковку кадров, вычисление CRC-8 и извлечение сообщений из потока.

## Формат кадра

```
[SYNC][FROM][TO][CMD][LEN][DATA...][CRC]
```

- **SYNC**: 0xAA
- **FROM**: адрес отправителя (1 байт)
- **TO**: адрес получателя (1 байт)
- **CMD**: команда (1 байт)
- **LEN**: длина данных (0..26)
- **DATA**: полезная нагрузка (0..26 байт)
- **CRC**: контрольная сумма CRC-8 (Dallas/Maxim, полином 0x31)

Максимальный размер кадра: 32 байта.

## Возможности

- **Упаковка/распаковка**: `proto_pack()`, `proto_unpack()`
- **CRC-8**: `proto_crc8()`
- **Потоковый парсер**: конечный автомат для приёма по одному байту
- **Пакетная обработка**: `proto_parser_feed_batch()`
- **C/C++ совместимость**: заголовочный файл поддерживает оба языка
- **Минимальные зависимости**: только stdint.h, stddef.h

## Использование

Подробная документация по функциям находится в заголовочном файле [`small_protocol.h`](small_protocol.h). Используйте Doxygen для генерации документации.

### Быстрый старт

```c
#include "small_protocol.h"

// Создание сообщения
proto_msg_t msg = PROTO_MSG_INIT(0x01, 0x02, 0x10, data, data_len);

// Упаковка
uint8_t buf[PROTO_MAX_FRAME];
int packed_len = proto_pack(&msg, buf);

// Распаковка
proto_msg_t out_msg;
int unpack_result = proto_unpack((proto_t*)buf, &out_msg);

// Потоковый парсер
proto_parser_t parser;
proto_parser_init(&parser);

uint8_t byte = 0xAA;
proto_parser_result_t res = proto_parser_feed(&parser, byte, &out_msg);
```

## Сборка

Библиотека состоит из двух файлов:
- `small_protocol.h` – заголовочный файл
- `small_protocol.c` – реализация

Добавьте оба файла в проект и скомпилируйте с поддержкой C99 или новее.

### Тестирование

Для запуска модульных тестов выполните:

```bash
gcc -Wall -Wextra -std=c99 -I. small_protocol.c test_small_protocol.c -o test_small_protocol
./test_small_protocol
```

Тесты используют фреймворк MinUnit и проверяют:
- корректность упаковки/распаковки
- вычисление CRC
- работу парсера (одиночные байты и пакеты)
- обработку ошибок (неверный CRC, переполнение)

## Лицензия

MIT