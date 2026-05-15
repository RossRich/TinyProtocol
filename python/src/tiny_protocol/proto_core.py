from dataclasses import dataclass

# [SYNC][SYS_ID][TARGET_ID][CMD][LEN][DATA...][CRC]

PROTO_SYNC = 0xAA
PROTO_MAX_FRAME = 32
PROTO_MAX_HEADER = 5  # SYNC | SYS_ID | TARGET_ID | CMD | LEN
PROTO_MAX_DATA = 26  # PROTO_MAX_FRAME - 1(sync) - 4(meta) - 1(CRC)

PROTO_SYNC_POS = 0
PROTO_SYS_ID_POS = 1
PROTO_TARGET_ID_POS = 2
PROTO_CMD_POS = 3
PROTO_LEN_POS = 4
PROTO_DATA_POS = 5

ADDR_BROADCAST = 0xFF


@dataclass(frozen=True)
class ProtoMsg():
  from_addr: int
  to_arrd: int
  cmd: int
  data: bytes

  @property
  def size(self) -> int:
    return (PROTO_MAX_HEADER - 1) + len(self.data)


@dataclass
class Proto:
  header: bytearray
  data: bytearray
  crc: int = 0
