"""
Stage-3 / Stage-4: Binary frame codec (stable transport layer)

Frame layout (little-endian):
SOF(2)=0xAA55 | VER(1)=0x01 | TYPE(1) | SEQ(2) | LEN(4) | PAYLOAD(LEN) | CRC32(4) | EOF(2)=0x55AA

CRC32 is computed over PAYLOAD only (IEEE 802.3 / zlib.crc32).

这个文件是通信协议的“底层传输层核心资产”：
负责把“字节流” <-> “单帧结构”互相转换。

设计目标（核心思想）：
- 传输层（TCP/UART/USB CDC等）永远是数据流（stream）：可能粘包 / 拆包 / 中途插入垃圾字节
- 协议层在流上“切帧”：靠 SOF + LEN + CRC + EOF 恢复帧边界与完整性
- 一旦出错：宁可丢掉当前帧，也不要影响后续帧解析（resync 机制）

注意：
- 本文件只负责“传输层帧”
- PAYLOAD 内部可以承载不同应用层语义：
    * TYPE_REQ          : 请求帧
    * TYPE_RESULT       : 结果帧
    * TYPE_IMAGE_CHUNK  : 图像分片帧（其分片协议定义在上层文件中）
- 也就是说，图像分片协议不应修改本文件帧头结构，而应定义在 PAYLOAD 内部

帧格式（Little-endian）：
    SOF(2 Bytes) = 0xAA55
    VER(1 Byte)  = 0x01
    TYPE(1 Byte)
    SEQ(2 Bytes)
    LEN(4 Bytes)
    PAYLOAD(LEN Bytes)
    CRC32(4 Bytes)  # 只对 PAYLOAD 计算 CRC32
    EOF(2 Bytes) = 0x55AA

文件提供两个核心能力：
1) pack_frame(payload)  -> bytes
2) try_parse_frame(buf) -> (frame?, remaining_buf, err?)

它可以同时用于：
- PC 端：从 socket / serial 接收字节流，循环 try_parse_frame() 抽取帧
- MCU / 模拟端：把任意 payload 打包成帧发送
"""


from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib
from typing import Optional, Tuple

# ----------------------------
# [模块 1] 协议常量定义
# ----------------------------
# SOF/EOF 是用来在数据流中定位帧边界的“同步标记”。
# 选 2 字节是折中：比 1 字节更不容易误匹配；比更长标记开销更小。
SOF = b"\xAA\x55"
EOF = b"\x55\xAA"
# 协议版本号，未来可扩展
VER = 0x01
# 最大允许的 payload 长度（防止内存耗尽攻击）
#目的：防止因为LEN被干扰解析成一个巨大的数，导致接收端一直等不到“那么多字节”，从而内存耗尽。
DEFAULT_MAX_LEN = 2_000_000  # 2 MB

# 帧类型定义
TYPE_REQ = 0x01
TYPE_RESULT = 0x02
TYPE_IMAGE_CHUNK = 0x03

# ----------------------------
# [模块 2] 数据结构：Frame
# ----------------------------
@dataclass(frozen=True)
class Frame:
    '''
    解析成功后返回的“帧对象”
    -ver:协议版本（应为 VER=0x01)
    -TYPE:帧类型（0x01=JPEG,未来可扩展ACK/RESULT/RAW等）
    -seq:帧序号(uint16),用于统计丢帧/对齐日志
    -payload:负载数据（例如 JPEG Bytes）
    -crc32:payload的CRC32校验值
    '''
    ver: int
    type: int
    seq: int
    payload: bytes
    crc32: int


# ----------------------------
# [模块 3] CRC32 工具函数
# ----------------------------
def crc32_ieee(data: bytes) -> int:
    '''
    计算CRC32(IEEE 802.3常用形式).

    zlib.crc32()在python里是最常用的CRC32实现之一
    & 0xFFFFFFFF 的原因：保证结果是无符号 32 位整数（Python int 没有溢出）。
    '''
    return zlib.crc32(data) & 0xFFFFFFFF

# ----------------------------
# [模块 4] 打包：payload -> frame bytes
# ----------------------------
def pack_frame(payload: bytes, *, frame_type: int = 0x01, seq: int = 0) -> bytes:
    """
    将 payload 打包为协议帧，返回可直接发送的 bytes。

    参数：
    - payload: 要传输的原始字节（例如 JPEG 文件 bytes）
    - frame_type: 帧类型（默认 0x01=JPEG）
    - seq: 帧序号（默认 0）

    返回：
    - header + payload + tail

    header（固定 10 字节）格式：
    <2s BB H I
    - 2s : SOF（2字节）
    - B  : VER（1字节）
    - B  : TYPE（1字节）
    - H  : SEQ（2字节，uint16，小端）
    - I  : LEN（4字节，uint32，小端）

    tail（固定 6 字节）格式：
    <I 2s
    - I  : CRC32（4字节）
    - 2s : EOF（2字节）
    """
    # --- 类型检查：防止传入 str 或其他对象导致隐式错误 ---
    if not isinstance(payload, (bytes, bytearray, memoryview)):
        raise TypeError("payload must be bytes-like")
    payload = bytes(payload) # 统一成Bytes(不可变)，避免后续被外部修改
    length = len(payload)

    #  对payload计算CRC32(只校验payload)
    crc = crc32_ieee(payload)
    # struct.pack 的 '<' 表示小端；字段顺序必须与协议一致
    header = struct.pack("<2sBBHI", SOF, VER, frame_type & 0xFF, seq & 0xFFFF, length)
    # frame_type & 0xFF 确保只截断到1字节，seq & 0xFFFF 确保只截断到2字节
    tail = struct.pack("<I2s", crc, EOF)
    return header + payload + tail

# ----------------------------
# [模块 5] 解析：从流缓冲中“尝试抽取一帧”
# ----------------------------
def try_parse_frame(
    buffer: bytes,
    *,
    max_len: int = DEFAULT_MAX_LEN,
) -> Tuple[Optional[Frame], bytes, Optional[str]]:
    """
    尝试从 buffer（字节流缓冲）中解析“恰好一帧”。

    重要：buffer 代表“流式接收缓冲”，可能出现：
    - 粘包：一次 recv 收到多帧
    - 拆包：一帧分多次 recv 才收齐
    - 噪声：中间插入无效字节导致错位

    返回三元组：
    1) frame_or_none:
       - 成功解析到 1 帧 -> Frame(...)
       - 当前数据不足以组成完整帧 -> None
       - 数据损坏/需要重同步 -> None
    2) remaining_buffer:
       - 成功：返回“去掉这帧后的剩余字节”（可能包含下一帧的起始）
       - 失败重同步：可能丢弃部分数据后返回剩余
    3) error_reason_or_none:
       - None 表示“没有错误”（可能是数据不够）
       - 字符串表示错误原因，便于日志与验收区分

    解析策略（关键工程取舍）：
    - 找 SOF：对齐帧头（如果找不到 SOF，清空 buffer，避免无限增长）
    - 读 header：拿到 LEN
    - 校验 LEN 合理性：避免等待巨量数据
    - 等待完整帧：如果 buffer 不够，直接返回 (None, buffer, None)，让上层继续 recv
    - 校验 EOF & CRC32：失败则丢帧（drop frame），继续尝试后续帧
    """
    # 情况 0：缓冲为空
    if not buffer:
        return None, buffer, None
    # 1) 在流缓冲中查找 SOF（同步头）
    #    目的：允许在“错位/噪声”的情况下重新对齐。
    sof_idx = buffer.find(SOF)
    # 1.1) 完全找不到 SOF：说明 buffer 当前全是无效垃圾字节
    #      直接丢弃全部 buffer，避免内存无止境增长。  
    if sof_idx < 0:
        return None, b"", "NO_SOF_DROP_ALL"
    # 1.2) 如果 SOF 不在开头，丢掉 SOF 前的垃圾字节，实现重同步。
    if sof_idx > 0:
        buffer = buffer[sof_idx:]

    # Header: 10 bytes
    if len(buffer) < 10:
        # 数据不足，等待更多 recv
        return None, buffer, None
    # 3) 解包 header
    try:
        sof, ver, ftype, seq, length = struct.unpack("<2sBBHI", buffer[:10])
    except struct.error:
        # header 解包都失败，说明极度错位；丢掉 2 字节尝试重新找 SOF
        # 为什么丢 2 字节：SOF 是 2 字节，丢 2 字节更快滑动窗口，减少死循环概率。
        return None, buffer[2:], "HEADER_UNPACK_FAIL_RESYNC"
    # 4) Header 字段校验（防御性）
    if sof != SOF:
        # 理论上不会发生（因为前面 find(SOF) 已对齐），但仍做保护
        return None, buffer[1:], "SOF_MISMATCH_RESYNC"

    if ver != VER:
        # 版本不匹配：可能是未来版本或乱流噪声
        # 丢 2 字节重新同步，避免卡在错误版本上
        return None, buffer[2:], f"BAD_VER_{ver}_RESYNC"
    # LEN 合理性检查：避免等一个天文数字导致卡死/内存爆炸
    if length <= 0 or length > max_len:
        return None, buffer[2:], f"BAD_LEN_{length}_RESYNC"
    # 5) 计算完整帧总长度：
    #    header(10) + payload(length) + CRC32(4) + EOF(2)
    total_len = 10 + length + 4 + 2
    # 5.1) 数据不足：等待更多 recv
    if len(buffer) < total_len:
        return None, buffer, None
    # 6) 切出 payload 与尾部字段
    payload = buffer[10 : 10 + length]
    crc_recv, eof = struct.unpack("<I2s", buffer[10 + length : total_len])
    # 7) EOF 校验：帧尾不匹配说明错位/损坏
    if eof != EOF:
        # 丢 2 字节重同步（同上：快滑动窗口）
        return None, buffer[2:], "BAD_EOF_RESYNC"
    # 8) CRC 校验：识别传输损坏
    crc_calc = crc32_ieee(payload)
    if crc_calc != crc_recv:
        # CRC 失败：说明这一帧 payload 在传输过程中损坏（或发送端/接收端协议不一致）
        # 工程策略：丢弃整帧，继续尝试解析后续帧（remaining 可能含下一帧）
        remaining = buffer[total_len:]
        return None, remaining, "CRC_FAIL_DROP_FRAME"
    # 9) 成功：构造 Frame 对象并返回
    frame = Frame(ver=ver, type=ftype, seq=seq, payload=payload, crc32=crc_recv)
    remaining = buffer[total_len:]
    return frame, remaining, None