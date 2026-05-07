# ============================================================
# 模块定位：
# ------------------------------------------------------------
# 本文件定义“图像分片协议（IMAGE_CHUNK）”以及接收端重组逻辑
#
# 在整体系统中的位置：
#
#   发送端（MCU / 模拟）：
#       image_bytes
#           ↓
#       build_image_chunk_frames()
#           ↓
#       frame_codec.pack_frame()
#
#   接收端（PC）：
#       try_parse_frame()
#           ↓
#       ImageChunkAssembler.handle_frame()
#           ↓
#       ReassembledImage（完整JPEG）
#
# 设计目标：
#   - 不修改底层 frame_codec（保持阶段三稳定性）
#   - 在 payload 层扩展“图像分片语义”
#   - 将“分片重组逻辑”收敛到通信模块，而非业务层（AI）
#
# ============================================================
from __future__ import annotations

import math
import struct
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

from frame_codec import Frame, pack_frame


# ============================================================
# 应用层协议：IMAGE_CHUNK
# ------------------------------------------------------------
# 设计原则：
# 1) 不修改底层 frame_codec.py
# 2) 复用第三阶段已经稳定的 pack_frame / try_parse_frame
# 3) 在 payload 内部定义“图像分片协议”
#
# payload 格式（小端）：
#   IMG_ID(1B) | CHUNK_SEQ(2B) | CHUNK_TOTAL(2B) | CHUNK_DATA(nB)
#
# 注意：
# - frame.seq 是“传输层帧序号”
# - chunk_seq 是“应用层图像分片序号”
# 二者不要混用
# ============================================================


# ----------------------------
# [模块 1] 帧类型定义
# ----------------------------
TYPE_REQ = 0x01
TYPE_RESULT = 0x02
TYPE_IMAGE_CHUNK = 0x03


# ----------------------------
# [模块 2] 分片协议常量
# ----------------------------
# payload 前 5 字节是应用层头：
# IMG_ID(1B) + CHUNK_SEQ(2B) + CHUNK_TOTAL(2B)
CHUNK_PAYLOAD_HEADER_FMT = "<BHH"
CHUNK_PAYLOAD_HEADER_SIZE = struct.calcsize(CHUNK_PAYLOAD_HEADER_FMT)  # 5

# 默认单片数据大小（只算图像数据，不含应用层头）
DEFAULT_CHUNK_DATA_SIZE = 256

# 默认缓存超时（秒）
DEFAULT_ASSEMBLY_TIMEOUT_SEC = 5.0


# ----------------------------
# [模块 3] 数据结构
# ----------------------------
@dataclass(frozen=True)
class ChunkMeta:
    """
    解析 IMAGE_CHUNK payload 后得到的元信息
    """
    img_id: int
    chunk_seq: int
    chunk_total: int
    chunk_data: bytes


@dataclass(frozen=True)
class ReassembledImage:
    """
    一张完整重组后的图像
    """
    img_id: int
    total_chunks: int
    data: bytes


# ----------------------------
# [模块 4] payload 打包 / 解析
# ----------------------------
def build_image_chunk_payload(
    img_id: int,
    chunk_seq: int,
    chunk_total: int,
    chunk_data: bytes,
) -> bytes:
    """
    构造 IMAGE_CHUNK 的 payload

    payload 布局：
        IMG_ID(1B) | CHUNK_SEQ(2B) | CHUNK_TOTAL(2B) | CHUNK_DATA(nB)
    """
    if not isinstance(chunk_data, (bytes, bytearray, memoryview)):
        raise TypeError("chunk_data must be bytes-like")

    if not (0 <= img_id <= 0xFF):
        raise ValueError("img_id must be in range [0, 255]")

    if not (0 <= chunk_seq <= 0xFFFF):
        raise ValueError("chunk_seq must be in range [0, 65535]")

    if not (1 <= chunk_total <= 0xFFFF):
        raise ValueError("chunk_total must be in range [1, 65535]")

    if chunk_seq >= chunk_total:
        raise ValueError("chunk_seq must be less than chunk_total")

    chunk_data = bytes(chunk_data)
    header = struct.pack(
        CHUNK_PAYLOAD_HEADER_FMT,
        img_id,
        chunk_seq,
        chunk_total,
    )
    return header + chunk_data


def parse_image_chunk_payload(payload: bytes) -> ChunkMeta:
    """
    解析 IMAGE_CHUNK payload
    """
    if not isinstance(payload, (bytes, bytearray, memoryview)):
        raise TypeError("payload must be bytes-like")

    payload = bytes(payload)

    if len(payload) < CHUNK_PAYLOAD_HEADER_SIZE:
        raise ValueError(
            f"payload too short for image chunk header: {len(payload)} bytes"
        )

    img_id, chunk_seq, chunk_total = struct.unpack(
        CHUNK_PAYLOAD_HEADER_FMT,
        payload[:CHUNK_PAYLOAD_HEADER_SIZE],
    )
    chunk_data = payload[CHUNK_PAYLOAD_HEADER_SIZE:]

    if chunk_total == 0:
        raise ValueError("chunk_total must not be 0")

    if chunk_seq >= chunk_total:
        raise ValueError(
            f"invalid chunk_seq/chunk_total: seq={chunk_seq}, total={chunk_total}"
        )

    return ChunkMeta(
        img_id=img_id,
        chunk_seq=chunk_seq,
        chunk_total=chunk_total,
        chunk_data=chunk_data,
    )


# ----------------------------
# [模块 5] 图像 -> 分片 -> frame
# 作用：
#   将“完整图像”转换为“可传输的多帧结构”
#
# 注意：
#   这是发送端唯一需要调用的入口函数
#
# 数据流：
#   image_bytes → chunks → payload → frame → bytes
# ------------------------------------------------------------
def split_bytes_to_chunks(
    data: bytes,
    *,
    chunk_data_size: int = DEFAULT_CHUNK_DATA_SIZE,
) -> List[bytes]:
    """
    纯数据切片，不做任何协议包装
    """
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("data must be bytes-like")

    if chunk_data_size <= 0:
        raise ValueError("chunk_data_size must be > 0")

    data = bytes(data)

    if len(data) == 0:
        raise ValueError("data must not be empty")

    chunks: List[bytes] = []
    for i in range(0, len(data), chunk_data_size):
        chunks.append(data[i:i + chunk_data_size])

    return chunks


def build_image_chunk_frames(
    image_bytes: bytes,
    *,
    img_id: int,
    chunk_data_size: int = DEFAULT_CHUNK_DATA_SIZE,
    frame_seq_start: int = 0,
) -> List[bytes]:
    """
    将一张完整图片切片，并直接打包为底层 frame bytes 列表

    返回值：
        [frame0_bytes, frame1_bytes, ...]
    """
    chunks = split_bytes_to_chunks(image_bytes, chunk_data_size=chunk_data_size)
    total = len(chunks)

    frames: List[bytes] = []
    frame_seq = frame_seq_start & 0xFFFF

    for chunk_seq, chunk_data in enumerate(chunks):
        payload = build_image_chunk_payload(
            img_id=img_id,
            chunk_seq=chunk_seq,
            chunk_total=total,
            chunk_data=chunk_data,
        )
        frame_bytes = pack_frame(
            payload,
            frame_type=TYPE_IMAGE_CHUNK,
            seq=frame_seq,
        )
        frames.append(frame_bytes)
        frame_seq = (frame_seq + 1) & 0xFFFF

    return frames


# ----------------------------
# [模块 6] 接收端缓存结构
# 作用：
#   表示“某一张图片在接收过程中的中间状态”
#
# 特点：
#   - 支持乱序接收（通过 chunk_seq 存储）
#   - 自动去重（重复分片忽略）
#   - 支持最终按序重组
#
# 生命周期：
#   创建 → 持续接收 → 完整 → assemble → 删除
# ----------------------------
class _ImageAssemblyBuffer:
    """
    某一张图片的接收缓存
    """

    def __init__(self, img_id: int, total_chunks: int) -> None:
        self.img_id = img_id
        self.total_chunks = total_chunks
        self.chunks: Dict[int, bytes] = {}
        self.created_at = time.time()
        self.updated_at = self.created_at

    @property
    def received_count(self) -> int:
        return len(self.chunks)

    def add_chunk(self, chunk_seq: int, chunk_data: bytes) -> bool:
        """
        返回值：
        - True  : 新增成功
        - False : 重复分片（已存在）
        """
        self.updated_at = time.time()

        if chunk_seq in self.chunks:
            return False

        self.chunks[chunk_seq] = chunk_data
        return True

    def is_complete(self) -> bool:
        return self.received_count == self.total_chunks

    def assemble(self) -> bytes:
        """
        按 seq 顺序拼接
        """
        if not self.is_complete():
            raise RuntimeError(
                f"image not complete: {self.received_count}/{self.total_chunks}"
            )

        data = bytearray()
        for seq in range(self.total_chunks):
            if seq not in self.chunks:
                raise RuntimeError(
                    f"missing chunk during assembly: img_id={self.img_id}, seq={seq}"
                )
            data.extend(self.chunks[seq])

        return bytes(data)


# ----------------------------
# [模块 7] 接收端重组器
# 核心职责：
#   将“离散的 IMAGE_CHUNK 帧”转换为“完整图像对象”
#
# 这是整个系统中：
#   ⭐ “通信层 → 业务层（AI）” 的唯一入口
#
# 设计意义：
#   - 上层（AI）无需关心分片细节
#   - 下层（frame_codec）无需理解图像语义
#
# 返回机制：
#   None → 继续等待
#   ReassembledImage → 一张完整图像已就绪
# ----------------------------
class ImageChunkAssembler:
    """
    通信模块中的“图像分片重组器”

    用法：
        assembler = ImageChunkAssembler()

        # 当 try_parse_frame() 成功拿到 frame 后：
        result = assembler.handle_frame(frame)

        if result is not None:
            # result 是 ReassembledImage
            print(result.img_id, len(result.data))
    """

    def __init__(self, *, timeout_sec: float = DEFAULT_ASSEMBLY_TIMEOUT_SEC) -> None:
        if timeout_sec <= 0:
            raise ValueError("timeout_sec must be > 0")

        self.timeout_sec = timeout_sec
        self._buffers: Dict[int, _ImageAssemblyBuffer] = {}

    def handle_frame(self, frame: Frame) -> Optional[ReassembledImage]:
        """
        输入一帧；若刚好凑齐一张完整图像，则返回 ReassembledImage
        否则返回 None
        """
        if not isinstance(frame, Frame):
            raise TypeError("frame must be Frame")

        # 只处理 IMAGE_CHUNK
        if frame.type != TYPE_IMAGE_CHUNK:
            return None

        meta = parse_image_chunk_payload(frame.payload)

        # 先清理超时缓存，避免累积
        self.cleanup()

        # 初始化或检查当前 img_id 的缓存
        if meta.img_id not in self._buffers:
            self._buffers[meta.img_id] = _ImageAssemblyBuffer(
                img_id=meta.img_id,
                total_chunks=meta.chunk_total,
            )

        buf = self._buffers[meta.img_id]

        # total 不一致，说明同一 img_id 对应的图片批次冲突
        # 工程策略：直接丢弃旧缓存并重建
        if buf.total_chunks != meta.chunk_total:
            self._buffers[meta.img_id] = _ImageAssemblyBuffer(
                img_id=meta.img_id,
                total_chunks=meta.chunk_total,
            )
            buf = self._buffers[meta.img_id]

        # 新增 chunk（重复 chunk 自动忽略）
        buf.add_chunk(meta.chunk_seq, meta.chunk_data)

        # 若收齐，立即重组并删除缓存
        if buf.is_complete():
            image_bytes = buf.assemble()
            del self._buffers[meta.img_id]
            return ReassembledImage(
                img_id=meta.img_id,
                total_chunks=meta.chunk_total,
                data=image_bytes,
            )

        return None

    def cleanup(self) -> List[int]:
        """
        清理超时未完成的图片缓存
        返回被删除的 img_id 列表
        """
        now = time.time()
        expired_ids: List[int] = []

        for img_id, buf in list(self._buffers.items()):
            if now - buf.updated_at > self.timeout_sec:
                expired_ids.append(img_id)
                del self._buffers[img_id]

        return expired_ids

    def clear(self) -> None:
        """
        清空所有缓存
        """
        self._buffers.clear()

    def snapshot(self) -> Dict[int, Dict[str, int]]:
        """
        调试辅助：查看当前缓存状态
        """
        state: Dict[int, Dict[str, int]] = {}
        for img_id, buf in self._buffers.items():
            state[img_id] = {
                "total_chunks": buf.total_chunks,
                "received_count": buf.received_count,
            }
        return state


# ----------------------------
# [模块 8] 实用辅助函数
# ----------------------------
def save_bytes_to_file(data: bytes, path: str) -> None:
    """
    将重组后的 bytes 保存成文件
    """
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("data must be bytes-like")

    with open(path, "wb") as f:
        f.write(bytes(data))


def load_bytes_from_file(path: str) -> bytes:
    """
    从文件读取完整 bytes
    """
    with open(path, "rb") as f:
        return f.read()


# ----------------------------
# [模块 9] 简单自测
# ----------------------------
if __name__ == "__main__":
    # 1) 读取一张图片
    src_path = "test.jpg"
    image_bytes = load_bytes_from_file(src_path)

    # 2) 打包成多帧
    frames = build_image_chunk_frames(
        image_bytes,
        img_id=1,
        chunk_data_size=256,
        frame_seq_start=100,
    )
    print(f"[TX] total frames = {len(frames)}")

    # 3) 模拟接收端：这里假设你外层已经用 try_parse_frame() 解析出了 Frame
    #    为了演示，这里直接“再拆一次底层 frame”是不合适的；
    #    所以实际项目中你应在 socket/serial 接收端得到 frame 后调用 handle_frame()
    #
    #    这里给一个“伪流程”示意：
    from frame_codec import try_parse_frame

    assembler = ImageChunkAssembler(timeout_sec=5.0)
    recv_buffer = b""

    reassembled: Optional[ReassembledImage] = None

    for frame_bytes in frames:
        # 模拟“流式到达”
        recv_buffer += frame_bytes

        while True:
            frame, recv_buffer, err = try_parse_frame(recv_buffer)
            if err:
                print(f"[RX] parse err = {err}")

            if frame is None:
                break

            result = assembler.handle_frame(frame)
            if result is not None:
                reassembled = result
                print(
                    f"[RX] image complete: img_id={result.img_id}, "
                    f"chunks={result.total_chunks}, bytes={len(result.data)}"
                )

    # 4) 验证重组一致性
    if reassembled is None:
        raise RuntimeError("reassembly failed: no complete image produced")

    assert reassembled.data == image_bytes, "reassembled image does not match source"
    print("[OK] reassembled bytes match source bytes")

    # 5) 保存重组结果（可选）
    out_path = "reassembled_test.jpg"
    save_bytes_to_file(reassembled.data, out_path)
    print(f"[OK] saved to {out_path}")