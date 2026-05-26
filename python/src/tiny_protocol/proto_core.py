from dataclasses import dataclass

# [SYNC][LEN][SYS_ID][TARGET_ID][MSG_ID][DATA...][CRC]

PROTO_SYNC = 0xAA
PROTO_MAX_FRAME_SIZE = 32
PROTO_HEADER_SIZE = 5  # LEN, SYS_ID, TARGET_ID, MSG_ID (без SYNC)
# PROTO_MAX_FRAME_SIZE - 1(sync) - PROTO_HEADER_SIZE - 1(CRC)
PROTO_MAX_PAYLOAD_SIZE = 26

PROTO_SYNC_POS = 0
PROTO_LEN_POS = 1
PROTO_SYS_ID_POS = 2
PROTO_TARGET_ID_POS = 3
PROTO_MSG_ID_POS = 4
PROTO_DATA_POS = 5

PROTO_BROADCAST_ADDR = 0xFF

# Маски для битов MSG_ID
PROTO_MSG_ID_MASK = 0x7F  # Младшие 7 бит - идентификатор сообщения (0-127)
PROTO_FLAG_ACK = (1 << 7)  # Старший бит - флаг ACK


@dataclass(frozen=True)
class ProtoMsg():
  from_addr: int
  to_arrd: int
  msg_id: int  # бит7=ACK, биты0-6=ID
  data: bytes

  @property
  def size(self) -> int:
    return (PROTO_HEADER_SIZE - 1) + len(self.data)


@dataclass
class Proto:
  header: bytearray
  data: bytearray
  crc: int = 0
