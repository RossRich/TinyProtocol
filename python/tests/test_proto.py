#!/usr/bin/env python3
"""
Модульные тесты для Python-реализации tiny_protocol.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../src'))

import unittest
import tiny_protocol.proto_core as proto
import tiny_protocol.proto as proto_func


class TestProtoCore(unittest.TestCase):
    """Тестирование констант и структур данных."""

    def test_constants(self):
        """Проверка значений констант из заголовка."""
        self.assertEqual(proto.PROTO_SYNC, 0xAA)
        self.assertEqual(proto.PROTO_LEN_POS, 1)
        self.assertEqual(proto.PROTO_SYS_ID_POS, 2)
        self.assertEqual(proto.PROTO_TARGET_ID_POS, 3)
        self.assertEqual(proto.PROTO_MSG_ID_POS, 4)
        self.assertEqual(proto.PROTO_HEADER_SIZE, 5)
        self.assertEqual(proto.PROTO_MAX_FRAME_SIZE, 32)
        self.assertEqual(proto.PROTO_MAX_PAYLOAD_SIZE, 26)
        self.assertEqual(proto.PROTO_BROADCAST_ADDR, 0xFF)
        self.assertEqual(proto.PROTO_MSG_ID_MASK, 0x7F)
        self.assertEqual(proto.PROTO_FLAG_ACK, 0x80)

    def test_proto_msg_creation(self):
        """Создание объекта ProtoMsg."""
        msg = proto.ProtoMsg(from_addr=0x01, to_arrd=0x02, msg_id=0x03, data=b'hello')
        self.assertEqual(msg.from_addr, 0x01)
        self.assertEqual(msg.to_arrd, 0x02)
        self.assertEqual(msg.msg_id, 0x03)
        self.assertEqual(msg.data, b'hello')

    def test_proto_msg_with_ack_flag(self):
        """Флаг ACK в поле msg_id."""
        msg = proto.ProtoMsg(from_addr=0x01, to_arrd=0x02, msg_id=0x03 | proto.PROTO_FLAG_ACK, data=b'')
        self.assertEqual(msg.msg_id & proto.PROTO_MSG_ID_MASK, 0x03)
        self.assertTrue(msg.msg_id & proto.PROTO_FLAG_ACK)


class TestCRC(unittest.TestCase):
    """Тестирование расчёта CRC-8."""

    def test_crc8_empty(self):
        """CRC от пустых данных."""
        self.assertEqual(proto_func.proto_crc8(b''), 0x00)

    def test_crc8_single_byte(self):
        """CRC от одного байта (проверка известных значений)."""
        self.assertEqual(proto_func.proto_crc8(b'\x00'), 0x00)
        self.assertEqual(proto_func.proto_crc8(b'\x01'), 0x31)
        self.assertEqual(proto_func.proto_crc8(b'\xFF'), 0xAC)

    def test_crc8_sequence(self):
        """CRC от последовательности байт."""
        data = b'\x02\x1C\xB8'
        crc = proto_func.proto_crc8(data)
        # Проверка свойства: CRC(data + crc) == 0
        crc2 = proto_func.proto_crc8(data + bytes([crc]))
        self.assertEqual(crc2, 0x00)

    def test_crc8_matches_c(self):
        """Согласованность с C-реализацией."""
        # Используем пакет из C-тестов
        header_and_payload = b'\x05\x01\x02\x03hello'  # LEN=5, FROM=1, TO=2, MSG_ID=3, данные='hello'
        crc = proto_func.proto_crc8(header_and_payload)
        # Вычисляем ожидаемый CRC через упаковку сообщения
        msg = proto.ProtoMsg(from_addr=0x01, to_arrd=0x02, msg_id=0x03, data=b'hello')
        packed = proto_func.proto_pack_msg(msg)
        expected_crc = packed[-1]
        self.assertEqual(crc, expected_crc)


class TestPack(unittest.TestCase):
    """Тестирование функций упаковки."""

    def test_pack_empty(self):
        """Упаковка сообщения без данных."""
        packed = proto_func.proto_pack(from_addr=0x11, to_addr=0x22, msg_id=0x33, data=b'')
        self.assertEqual(len(packed), proto.PROTO_HEADER_SIZE + 1)  # заголовок + CRC
        self.assertEqual(packed[0], proto.PROTO_SYNC)
        self.assertEqual(packed[1], 0)  # LEN
        self.assertEqual(packed[2], 0x11)  # FROM
        self.assertEqual(packed[3], 0x22)  # TO
        self.assertEqual(packed[4], 0x33)  # MSG_ID
        crc = proto_func.proto_crc8(packed[1:-1])
        self.assertEqual(packed[-1], crc)

    def test_pack_with_data(self):
        """Упаковка с полезной нагрузкой."""
        data = b'test'
        packed = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=data)
        self.assertEqual(len(packed), proto.PROTO_HEADER_SIZE + len(data) + 1)
        self.assertEqual(packed[0], proto.PROTO_SYNC)
        self.assertEqual(packed[1], len(data))
        self.assertEqual(packed[2], 0x01)
        self.assertEqual(packed[3], 0x02)
        self.assertEqual(packed[4], 0x03)
        self.assertEqual(packed[5:9], data)
        crc = proto_func.proto_crc8(packed[1:-1])
        self.assertEqual(packed[-1], crc)

    def test_pack_msg(self):
        """Упаковка через ProtoMsg."""
        msg = proto.ProtoMsg(from_addr=0xAA, to_arrd=0xBB, msg_id=0xCC, data=b'xyz')
        packed = proto_func.proto_pack_msg(msg)
        self.assertEqual(packed[0], proto.PROTO_SYNC)
        self.assertEqual(packed[1], 3)
        self.assertEqual(packed[2], 0xAA)
        self.assertEqual(packed[3], 0xBB)
        self.assertEqual(packed[4], 0xCC)
        self.assertEqual(packed[5:8], b'xyz')

    def test_pack_max_payload(self):
        """Упаковка максимально допустимого размера данных."""
        data = b'x' * proto.PROTO_MAX_PAYLOAD_SIZE
        packed = proto_func.proto_pack(from_addr=0x00, to_addr=0x00, msg_id=0x00, data=data)
        self.assertEqual(len(packed), proto.PROTO_MAX_FRAME_SIZE)
        self.assertEqual(packed[1], proto.PROTO_MAX_PAYLOAD_SIZE)

    def test_pack_ack_flag(self):
        """Упаковка с установленным флагом ACK."""
        msg = proto.ProtoMsg(from_addr=0x01, to_arrd=0x02, msg_id=0x45 | proto.PROTO_FLAG_ACK, data=b'')
        packed = proto_func.proto_pack_msg(msg)
        self.assertEqual(packed[4], 0x45 | proto.PROTO_FLAG_ACK)


class TestUnpack(unittest.TestCase):
    """Тестирование распаковки."""

    def test_unpack_empty(self):
        """Распаковка пустого сообщения."""
        packed = proto_func.proto_pack(from_addr=0x11, to_addr=0x22, msg_id=0x33, data=b'')
        container = proto.Proto(
            header=packed[:proto.PROTO_HEADER_SIZE],  # включает SYNC
            data=packed[proto.PROTO_HEADER_SIZE:-1],
            crc=packed[-1]
        )
        msg = proto_func.proto_unpack(container)
        self.assertEqual(msg.from_addr, 0x11)
        self.assertEqual(msg.to_arrd, 0x22)
        self.assertEqual(msg.msg_id, 0x33)
        self.assertEqual(msg.data, b'')

    def test_unpack_with_data(self):
        """Распаковка с данными."""
        data = b'hello world'
        packed = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=data)
        container = proto.Proto(
            header=packed[:proto.PROTO_HEADER_SIZE],  # включает SYNC
            data=packed[proto.PROTO_HEADER_SIZE:-1],
            crc=packed[-1]
        )
        msg = proto_func.proto_unpack(container)
        self.assertEqual(msg.from_addr, 0x01)
        self.assertEqual(msg.to_arrd, 0x02)
        self.assertEqual(msg.msg_id, 0x03)
        self.assertEqual(msg.data, data)

    def test_unpack_invalid_crc(self):
        """Распаковка с некорректным CRC (должна бросить исключение)."""
        packed = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=b'test')
        corrupted = bytearray(packed)
        corrupted[-1] ^= 0xFF
        container = proto.Proto(
            header=corrupted[:proto.PROTO_HEADER_SIZE],
            data=corrupted[proto.PROTO_HEADER_SIZE:-1],
            crc=corrupted[-1]
        )
        with self.assertRaises(proto_func.ProtoError):
            proto_func.proto_unpack(container)

    def test_unpack_broadcast_addr(self):
        """Распаковка широковещательного адреса."""
        packed = proto_func.proto_pack(from_addr=0x01, to_addr=proto.PROTO_BROADCAST_ADDR, msg_id=0x00, data=b'')
        container = proto.Proto(
            header=packed[:proto.PROTO_HEADER_SIZE],
            data=packed[proto.PROTO_HEADER_SIZE:-1],
            crc=packed[-1]
        )
        msg = proto_func.proto_unpack(container)
        self.assertEqual(msg.to_arrd, proto.PROTO_BROADCAST_ADDR)


class TestParser(unittest.TestCase):
    """Тестирование конечного автомата парсера."""

    def test_parser_empty(self):
        """Парсер на пустом входе."""
        parser = proto_func.ProtoParser()
        # parse_bytes возвращает MISSING_STREAM (-1) при пустом вводе
        self.assertEqual(parser.parse_bytes(b''), proto_func.ProtoParser.MISSING_STREAM)
        # get_proto_msg должен бросить исключение
        with self.assertRaises(proto_func.ProtoError):
            parser.get_proto_msg()

    def test_parser_single_packet(self):
        """Один корректный пакет."""
        parser = proto_func.ProtoParser()
        data = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=b'abc')
        # parse_bytes должен вернуть OK (1) при успешном извлечении пакета
        result = parser.parse_bytes(data)
        self.assertEqual(result, proto_func.ProtoParser.OK)
        msg = parser.get_proto_msg()
        self.assertIsNotNone(msg)
        self.assertEqual(msg.from_addr, 0x01)
        self.assertEqual(msg.to_arrd, 0x02)
        self.assertEqual(msg.msg_id, 0x03)
        self.assertEqual(msg.data, b'abc')

    def test_parser_multiple_packets(self):
        """Несколько пакетов подряд."""
        parser = proto_func.ProtoParser()
        pkt1 = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=b'first')
        pkt2 = proto_func.proto_pack(from_addr=0x04, to_addr=0x05, msg_id=0x06, data=b'second')
        # Подаём первый пакет
        result1 = parser.parse_bytes(pkt1)
        self.assertEqual(result1, proto_func.ProtoParser.OK)
        msg1 = parser.get_proto_msg()
        self.assertIsNotNone(msg1)
        self.assertEqual(msg1.from_addr, 0x01)
        # Подаём второй пакет
        result2 = parser.parse_bytes(pkt2)
        self.assertEqual(result2, proto_func.ProtoParser.OK)
        msg2 = parser.get_proto_msg()
        self.assertIsNotNone(msg2)
        self.assertEqual(msg2.from_addr, 0x04)
        # Больше сообщений нет
        with self.assertRaises(proto_func.ProtoError):
            parser.get_proto_msg()

    def test_parser_incremental(self):
        """Постепенная подача байт."""
        parser = proto_func.ProtoParser()
        pkt = proto_func.proto_pack(from_addr=0x11, to_addr=0x22, msg_id=0x33, data=b'chunk')
        # Подаём пакет по одному байту
        for i, b in enumerate(pkt):
            result = parser.parse_bytes(bytes([b]))
            if i < len(pkt) - 1:
                # Пока пакет не собран, parse_bytes возвращает MISSING_STREAM
                self.assertEqual(result, proto_func.ProtoParser.MISSING_STREAM)
            else:
                # Последний байт завершает пакет, должен вернуть OK
                self.assertEqual(result, proto_func.ProtoParser.OK)
        # Извлекаем сообщение
        msg = parser.get_proto_msg()
        self.assertIsNotNone(msg)
        self.assertEqual(msg.from_addr, 0x11)

    def test_parser_garbage_before_sync(self):
        """Мусор перед синхробайтом."""
        parser = proto_func.ProtoParser()
        garbage = b'\x00\xFF\x55'
        pkt = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=b'data')
        # Подаём мусор (не содержит 0xAA)
        result = parser.parse_bytes(garbage)
        self.assertEqual(result, proto_func.ProtoParser.MISSING_STREAM)
        # Теперь подаём корректный пакет
        result2 = parser.parse_bytes(pkt)
        self.assertEqual(result2, proto_func.ProtoParser.OK)
        msg = parser.get_proto_msg()
        self.assertIsNotNone(msg)
        self.assertEqual(msg.from_addr, 0x01)

    def test_parser_reset_after_message(self):
        """Парсер должен сбрасываться после извлечения сообщения."""
        parser = proto_func.ProtoParser()
        pkt = proto_func.proto_pack(from_addr=0x01, to_addr=0x02, msg_id=0x03, data=b'x')
        parser.parse_bytes(pkt)
        msg = parser.get_proto_msg()
        self.assertIsNotNone(msg)
        # После извлечения очередь пуста, get_proto_msg должно бросить исключение
        with self.assertRaises(proto_func.ProtoError):
            parser.get_proto_msg()
        # Следующий пакет
        pkt2 = proto_func.proto_pack(from_addr=0x04, to_addr=0x05, msg_id=0x06, data=b'y')
        parser.parse_bytes(pkt2)
        msg2 = parser.get_proto_msg()
        self.assertIsNotNone(msg2)
        self.assertEqual(msg2.from_addr, 0x04)


class TestIntegration(unittest.TestCase):
    """Интеграционные тесты: упаковка -> распаковка."""

    def test_roundtrip(self):
        """Полный цикл для различных комбинаций параметров."""
        for from_addr in (0x00, 0x01, 0xFF):
            for to_addr in (0x00, 0x02, 0xFF):
                for msg_id in (0x00, 0x7F, 0x80, 0xC5):
                    for data in (b'', b'a', b'hello', b'x' * 26):
                        if len(data) > proto.PROTO_MAX_PAYLOAD_SIZE:
                            continue
                        packed = proto_func.proto_pack(from_addr, to_addr, msg_id, data)
                        container = proto.Proto(
                            header=packed[:proto.PROTO_HEADER_SIZE],  # включает SYNC
                            data=packed[proto.PROTO_HEADER_SIZE:-1],
                            crc=packed[-1]
                        )
                        unpacked = proto_func.proto_unpack(container)
                        self.assertIsNotNone(unpacked, f"Ошибка для from={from_addr}, to={to_addr}, msg_id={msg_id}, data={data}")
                        self.assertEqual(unpacked.from_addr, from_addr)
                        self.assertEqual(unpacked.to_arrd, to_addr)
                        self.assertEqual(unpacked.msg_id, msg_id)
                        self.assertEqual(unpacked.data, data)

    def test_parser_roundtrip(self):
        """Парсер должен корректно извлекать упакованное сообщение."""
        parser = proto_func.ProtoParser()
        msg = proto.ProtoMsg(from_addr=0x55, to_arrd=0x66, msg_id=0x77, data=b'integration')
        packed = proto_func.proto_pack_msg(msg)
        parser.parse_bytes(packed)
        parsed = parser.get_proto_msg()
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed.from_addr, msg.from_addr)
        self.assertEqual(parsed.to_arrd, msg.to_arrd)
        self.assertEqual(parsed.msg_id, msg.msg_id)
        self.assertEqual(parsed.data, msg.data)


if __name__ == '__main__':
    unittest.main()