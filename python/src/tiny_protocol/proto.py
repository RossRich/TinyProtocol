from typing import List
import tiny_protocol.proto_core as proto


class ProtoError(ValueError):
  pass


def proto_crc8(data: bytes) -> int:
  """CRC-8 с полиномом 0x31 (Dallas/Maxim), идентично C-версии."""

  crc = 0
  for byte in data:
    crc ^= byte
    for _ in range(8):
      if crc & 0x80:
        crc = (crc << 1) ^ 0x31
      else:
        crc <<= 1
      crc &= 0xFF  # фиксируем 8 бит
  return crc


def proto_pack(from_addr: int, to_addr: int, cmd: int, data: bytes = b'') -> bytes:
  """
  Сериализует параметры сообщения в бинарный пакет протокола для передачи по UART.

  Формат пакета (бинарный, little-endian не требуется, так как поля <= 1 байта):
      [SYNC (1)] [FROM (1)] [TO (1)] [CMD (1)] [LEN (1)] [DATA (0..26)] [CRC8 (1)]

  Args:
      from_addr (int): Адрес отправителя (0x01–0xFE).
                        Значение 0x00 зарезервировано, 0xFF — broadcast.
      to_addr (int): Адрес получателя (0x01–0xFE).
                      Используйте 0xFF для широковещательной рассылки.
      cmd (int): Идентификатор команды (0x00–0xFF).
                  Определяет действие (например, 0x10 = SET_RELAY).
      data (bytes): Полезная нагрузка (аргументы команды).
                    Максимальный размер: 26 байт.
                    Если аргументов нет, оставьте пустым (b'').

  Returns:
      bytes: Готовый к отправке байтовый массив.
              Длина пакета варьируется от 6 (без данных) до 32 байт (макс. данные).

  Raises:
      ProtoError: Если адреса или команда выходят за диапазон 0–255.
      ProtoError: Если длина `data` превышает 26 байт (лимит буфера приема).

  Example:
      >>> # Включить реле №5 на устройстве 0x10
      >>> payload = bytes([5, 1])  # ID реле, состояние ON
      >>> pkt = proto_pack(0x01, 0x10, 0x10, payload)
      >>> ser.write(pkt)

      >>> # Запрос статуса без данных
      >>> pkt = proto_pack(0x01, 0x10, 0x20)
      >>> ser.write(pkt)
  """

  if not (0 <= from_addr <= 255):
    raise ProtoError(f"Invalid FROM address: {from_addr}")
  if not (0 <= to_addr <= 255):
    raise ProtoError(f"Invalid TO address: {to_addr}")
  if not (0 <= cmd <= 255):
    raise ProtoError(f"Invalid CMD: {cmd}")
  if len(data) > proto.PROTO_MAX_DATA:
    raise ProtoError(f"Data too long: {len(data)} > {proto.PROTO_MAX_DATA}")

  # 1. Формируем тело пакета без CRC
  pkt = bytearray()
  pkt.append(proto.PROTO_SYNC)
  pkt.append(from_addr)
  pkt.append(to_addr)
  pkt.append(cmd)
  pkt.append(len(data))
  pkt.extend(data)

  # 2. Считаем CRC по полям FROM..DATA (пропускаем SYNC)
  crc = proto_crc8(pkt[1:])
  pkt.append(crc)

  # 3. Возвращаем неизменяемый bytes (готов к отправке в pyserial/asyncio)
  return bytes(pkt)


def proto_pack_msg(msg: proto.ProtoMsg) -> bytes:
  """
  Сериализует объект ProtoMsg в бинарный пакет.

  Args:
      msg: Структурированное сообщение (адреса, команда, полезная нагрузка).

  Returns:
      bytes: Готовый байтовый массив формата [SYNC][FROM][TO][CMD][LEN][DATA][CRC].

  Note:
      Обёртка над proto_pack(). Позволяет передавать объект напрямую,
      без ручного извлечения полей. Идеально для очередей и колбэков.
  """

  return proto_pack(from_addr=msg.from_addr, to_addr=msg.to_arrd, cmd=msg.cmd, data=msg.data)


def proto_unpack(proto_cnt: proto.Proto) -> proto.ProtoMsg:
  """
  Десериализует пакета байт в сообщение ProtoMsg.

  Выполняет валидацию формата, проверку лимитов длины и верификацию
  контрольной суммы (CRC-8). При успешной проверке возвращает иммутабельный
  объект сообщения.

  Args:
      proto_cnt (proto.Proto): Контейнер с разобранным байтовым потоком.
          Ожидается следующая структура атрибутов:
          - header: Список/байты заголовка [FROM, TO, CMD, LEN].
          - data: Полезная нагрузка (0..26 байт).
          - crc: Целое число (байт контрольной суммы).

  Returns:
      proto.ProtoMsg: Объект сообщения с полями:
          - from_addr (int): Адрес отправителя
          - to_addr (int): Адрес получателя
          - cmd (int): Код команды
          - data (bytes): Полезная нагрузка (гарантированно неизменяемая)

  Raises:
      ProtoError: Если `header` отсутствует или пуст.
      ProtoError: Если длина данных превышает `proto.PROTO_MAX_DATA`.
      ProtoError: Если контрольная сумма не совпадает (пакет повреждён или потерян).

  Example:
      >>> # Обработка входящего потока UART
      >>> try:
      ...     msg = proto_unpack(parsed_container)
      ...     if msg.to_addr == MY_ADDR or msg.to_addr == 0xFF:
      ...         dispatch_command(msg)
      ... except ProtoError as e:
      ...     logger.warning(f"Invalid packet dropped: {e}")

  Note:
      - CRC-8 вычисляется по конкатенации `header[1:]` (данные без SYNC-байта)
        и `data`. Убедись, что `proto_pack` использует идентичную область расчёта.
      - Поле `data` приводится к типу `bytes` для гарантии иммутабельности.
        Это предотвращает побочные эффекты при асинхронной обработке.
    """

  if not proto_cnt.header:
    raise ProtoError("Invalid header: missing or empty")

  data_len = proto_cnt.header[proto.PROTO_LEN_POS]
  if data_len > proto.PROTO_MAX_DATA:
    raise ProtoError(f"Data too long: {data_len} > {proto.PROTO_MAX_DATA}")

  crc_payload = bytes(proto_cnt.header[1:]) + bytes(proto_cnt.data)
  crc = proto_crc8(crc_payload)
  if crc != proto_cnt.crc:
    raise ProtoError("Bad packet: CRC mismatch")

  from_addr = proto_cnt.header[proto.PROTO_SYS_ID_POS]
  to_addr = proto_cnt.header[proto.PROTO_TARGET_ID_POS]
  cmd = proto_cnt.header[proto.PROTO_CMD_POS]

  return proto.ProtoMsg(from_addr, to_addr, cmd, bytes(proto_cnt.data))


class ProtoParser():
  """
  Класс для поиска пакетов в потоке байт
  """

  OK = 1
  MISSING_STREAM = -1
  INVALID_CRC = -2

  def __init__(self) -> None:
    self._raw_bytes: bytearray = bytearray()
    self._frame: List[proto.Proto] = []

  def parse_bytes(self, in_stream: bytes) -> int:
    if not len(in_stream):
      return ProtoParser.MISSING_STREAM

    self._raw_bytes.extend(in_stream)

    # Ищем SYNC
    sync_idx = self._raw_bytes.find(proto.PROTO_SYNC)
    if sync_idx == -1:
      return ProtoParser.MISSING_STREAM

    # Удаляем мусор перед SYNC
    if sync_idx > 0:
      del self._raw_bytes[0:sync_idx]

    if len(self._raw_bytes) < proto.PROTO_MAX_HEADER:
      return ProtoParser.MISSING_STREAM

    data_len = self._raw_bytes[proto.PROTO_LEN_POS]
    if data_len > proto.PROTO_MAX_DATA:
      # не верный заголовок
      new_sync_idx = self._raw_bytes.find(proto.PROTO_SYNC, 1)
      if new_sync_idx != -1:
        del self._raw_bytes[0:new_sync_idx]
      else:
        self._raw_bytes.clear()
      return ProtoParser.MISSING_STREAM

    frame_len = proto.PROTO_MAX_HEADER + data_len + 1
    if len(self._raw_bytes) < frame_len:
      # обрывок пакета
      # TODO: добавить таймер
      return ProtoParser.MISSING_STREAM

    header = self._raw_bytes[:proto.PROTO_MAX_HEADER]
    data = self._raw_bytes[proto.PROTO_MAX_HEADER:proto.PROTO_MAX_HEADER + data_len]
    crc = self._raw_bytes[frame_len - 1]
    proto_frame = proto.Proto(header=header, data=data, crc=crc)
    self._frame.append(proto_frame)

    # удаляем обработанный фрейм
    del self._raw_bytes[0:frame_len]

    return ProtoParser.OK

  def get_proto_msg(self) -> proto.ProtoMsg:
    if not len(self._frame):
      raise ProtoError("No messages")
    return proto_unpack(self._frame.pop(0))
